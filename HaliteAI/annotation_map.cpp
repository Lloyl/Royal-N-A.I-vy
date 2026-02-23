/**
 * @file annotation_map.cpp
 * @brief Implémentation de la carte annotée (AnnotationMap).
 *
 * Ce module pré-calcule chaque tour les informations stratégiques utilisées
 * par les modules de planification, de spawn, de dropoff et de collision :
 *   - Champ d'attraction (blur exponentiel séparable en 2 passes)
 *   - Score de dominance (alliés − ennemis dans un rayon fixe)
 *   - Distances BFS multi-source O(W×H) vers alliés, ennemis, dropoffs
 *   - Inspiration (≥ N ennemis dans un rayon)
 *   - Portée ennemie (cases atteignables au prochain tour)
 */

#include "annotation_map.hpp"

#include <algorithm>
#include <cmath>

namespace hlt {

// ============================================================================
// Méthodes publiques
// ============================================================================

void AnnotationMap::build(Game& game) {
    game_map_    = game.game_map.get();
    my_id_       = game.my_id;
    width_       = game_map_->width;
    height_      = game_map_->height;
    num_players_ = static_cast<int>(game.players.size());

    reset();
    build_attraction_field();
    calculate_dominance(game);
    calculate_distances(game);
    calculate_inspiration(game);
    calculate_enemy_reach(game);
}

void AnnotationMap::mark_path(const std::vector<Position>& path,
                              int start_turn, bool mark_mining) {
    for (size_t i = 0; i < path.size(); ++i) {
        int turn_offset = start_turn + static_cast<int>(i);
        if (turn_offset < 64) {
            at_mut(path[i]).occupation_mask |= (1ULL << turn_offset);
        }
    }
    if (mark_mining && !path.empty()) {
        at_mut(path.back()).marked_for_mining = true;
    }
}

const CellInfo& AnnotationMap::at(const Position& pos) const {
    Position n = normalize(pos);
    return grid_[n.y][n.x];
}

CellInfo& AnnotationMap::at_mut(const Position& pos) {
    Position n = normalize(pos);
    return grid_[n.y][n.x];
}

void AnnotationMap::reset() {
    grid_.assign(height_, std::vector<CellInfo>(width_));
}

// ============================================================================
// Utilitaire interne
// ============================================================================

Position AnnotationMap::normalize(const Position& pos) const {
    int x = ((pos.x % width_)  + width_)  % width_;
    int y = ((pos.y % height_) + height_) % height_;
    return {x, y};
}

// ============================================================================
// Champ d'attraction — blur exponentiel séparable + seuil
// ============================================================================

void AnnotationMap::build_attraction_field() {
    /*
     * Le blur exponentiel en 2 passes (horizontale puis verticale) permet
     * d'obtenir un aperçu des zones riches en halite de la carte. Le facteur
     * de blur est adapté au nombre de joueurs pour ajuster la « dispersion »
     * de l'attraction (plus élevé en 4p, plus serré en 2p).
     *
     * Complexité : O(W × H) — une passe par dimension.
     */
    float blur_factor = (num_players_ <= 2) ? BLUR_FACTOR_2P : DEFAULT_BLUR_FACTOR;

    // Copier le halite brut dans une grille temporaire de floats
    std::vector<std::vector<float>> temp(height_, std::vector<float>(width_));
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            temp[y][x] = static_cast<float>(game_map_->cells[y][x].halite);
        }
    }

    // Passe horizontale (gauche → droite, puis droite → gauche, carte toroïdale)
    std::vector<std::vector<float>> blurred_h(height_, std::vector<float>(width_, 0.0f));
    for (int y = 0; y < height_; ++y) {
        // Gauche → droite
        float acc = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {  // 2 tours pour la toroïdalité
            for (int x = 0; x < width_; ++x) {
                acc = acc * blur_factor + temp[y][x] * (1.0f - blur_factor);
                if (pass == 1) blurred_h[y][x] = acc;
            }
        }
        // Droite → gauche
        acc = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            for (int x = width_ - 1; x >= 0; --x) {
                acc = acc * blur_factor + temp[y][x] * (1.0f - blur_factor);
                if (pass == 1) blurred_h[y][x] = (blurred_h[y][x] + acc) * 0.5f;
            }
        }
    }

    // Passe verticale (haut → bas, puis bas → haut, carte toroïdale)
    for (int x = 0; x < width_; ++x) {
        float acc = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 0; y < height_; ++y) {
                acc = acc * blur_factor + blurred_h[y][x] * (1.0f - blur_factor);
                if (pass == 1) grid_[y][x].attraction = acc;
            }
        }
        acc = 0.0f;
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = height_ - 1; y >= 0; --y) {
                acc = acc * blur_factor + blurred_h[y][x] * (1.0f - blur_factor);
                if (pass == 1) {
                    grid_[y][x].attraction = (grid_[y][x].attraction + acc) * 0.5f;
                }
            }
        }
    }

    // Appliquer le seuil : les cases sous le minimum d'attraction sont ignorées
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (grid_[y][x].attraction < ATTRACTION_THRESHOLD) {
                grid_[y][x].attraction = 0.0f;
            }
        }
    }
}

// ============================================================================
// Dominance — comptage alliés vs ennemis dans un rayon fixe
// ============================================================================

void AnnotationMap::calculate_dominance(Game& game) {
    /*
     * Pour chaque case de la carte, on compte combien de ships alliés et
     * ennemis se situent dans un rayon Manhattan DOMINANCE_RADIUS.
     * Le score de dominance = alliés − ennemis.
     * Score positif → zone contrôlée, négatif → zone hostile.
     */
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int allies  = 0;
            int enemies = 0;
            Position center(x, y);

            for (const auto& player : game.players) {
                bool is_ally = (player->id == my_id_);
                for (const auto& ship_pair : player->ships) {
                    int dist = game_map_->calculate_distance(center, ship_pair.second->position);
                    if (dist <= DOMINANCE_RADIUS) {
                        if (is_ally) ++allies;
                        else         ++enemies;
                    }
                }
            }
            grid_[y][x].dominance_score = allies - enemies;
        }
    }
}

// ============================================================================
// Distances BFS multi-source — O(W×H) par catégorie
// ============================================================================

void AnnotationMap::calculate_distances(Game& game) {
    /*
     * Trois BFS multi-source indépendantes remplissent dist_to_ally,
     * dist_to_enemy et dist_to_dropoff. Toutes les sources sont enfilées
     * au départ avec une distance de 0, et la BFS propage en O(W×H).
     */
    auto bfs = [&](std::queue<std::pair<Position, int>>& q,
                    auto CellInfo::* dist_field) {
        std::vector<std::vector<bool>> visited(height_, std::vector<bool>(width_, false));

        // Initialiser les sources déjà dans la queue
        auto q_copy = q;
        while (!q_copy.empty()) {
            auto [pos, d] = q_copy.front(); q_copy.pop();
            Position n = normalize(pos);
            visited[n.y][n.x] = true;
            grid_[n.y][n.x].*dist_field = d;
        }

        while (!q.empty()) {
            auto [pos, dist] = q.front(); q.pop();
            Position n = normalize(pos);

            for (auto d : ALL_CARDINALS) {
                Position neighbor = normalize(n.directional_offset(d));
                if (!visited[neighbor.y][neighbor.x]) {
                    visited[neighbor.y][neighbor.x] = true;
                    grid_[neighbor.y][neighbor.x].*dist_field = dist + 1;
                    q.push({neighbor, dist + 1});
                }
            }
        }
    };

    // BFS alliés
    {
        std::queue<std::pair<Position, int>> q;
        for (const auto& ship_pair : game.me->ships) {
            q.push({ship_pair.second->position, 0});
        }
        bfs(q, &CellInfo::dist_to_ally);
    }

    // BFS ennemis
    {
        std::queue<std::pair<Position, int>> q;
        for (const auto& player : game.players) {
            if (player->id == my_id_) continue;
            for (const auto& ship_pair : player->ships) {
                q.push({ship_pair.second->position, 0});
            }
        }
        bfs(q, &CellInfo::dist_to_enemy);
    }

    // BFS dropoffs + shipyard alliés
    {
        std::queue<std::pair<Position, int>> q;
        q.push({game.me->shipyard->position, 0});
        for (const auto& dropoff_pair : game.me->dropoffs) {
            q.push({dropoff_pair.second->position, 0});
        }
        bfs(q, &CellInfo::dist_to_dropoff);
    }
}

// ============================================================================
// Inspiration — comptage ennemis dans le rayon d'inspiration
// ============================================================================

void AnnotationMap::calculate_inspiration(Game& game) {
    if (!constants::INSPIRATION_ENABLED) return;

    /*
     * Pour chaque case, compter le nombre de ships ennemis dans un rayon
     * INSPIRATION_RADIUS. Si ce nombre ≥ INSPIRATION_SHIP_COUNT, la case
     * est marquée inspirée.
     */
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int enemy_count = 0;
            Position center(x, y);

            for (const auto& player : game.players) {
                if (player->id == my_id_) continue;
                for (const auto& ship_pair : player->ships) {
                    int dist = game_map_->calculate_distance(center, ship_pair.second->position);
                    if (dist <= constants::INSPIRATION_RADIUS) {
                        ++enemy_count;
                    }
                }
            }
            grid_[y][x].is_inspired = (enemy_count >= constants::INSPIRATION_SHIP_COUNT);
        }
    }
}

// ============================================================================
// Portée ennemie — cases atteignables par un ennemi au prochain tour
// ============================================================================

void AnnotationMap::calculate_enemy_reach(Game& game) {
    /*
     * Un ship ennemi peut rester sur place ou se déplacer d'exactement 1 case
     * dans chaque direction cardinale. On marque ces 5 positions comme
     * « enemy_reachable » ainsi que « has_enemy_ship » pour la position actuelle.
     */
    for (const auto& player : game.players) {
        if (player->id == my_id_) continue;
        for (const auto& ship_pair : player->ships) {
            Position pos = ship_pair.second->position;
            at_mut(pos).has_enemy_ship  = true;
            at_mut(pos).enemy_reachable = true;

            for (auto d : ALL_CARDINALS) {
                at_mut(pos.directional_offset(d)).enemy_reachable = true;
            }
        }
    }
}

} // namespace hlt
