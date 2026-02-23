/**
 * @file planning_strategy.cpp
 * @brief Implémentation du module de planification stratégique.
 *
 * Ce module assigne à chaque ship un rôle et un chemin pour le tour courant.
 * Les rôles sont traités par priorité décroissante (end-game → livraison →
 * minage local → minage global → minage greedy → dernier recours).
 * Le pathfinding repose sur Dijkstra avec des coûts distordus intégrant
 * le halite brûlé, la dominance ennemie et la proximité adverse.
 *
 * Le système de candidats trie les assignations par score et les attribue
 * de façon greedy, avec recalcul en cas de conflit de chemin.
 */

#include "planning_strategy.hpp"
#include "log.hpp"

#include <limits>
#include <sstream>

namespace hlt {

// ============================================================================
// Configuration selon le nombre de joueurs
// ============================================================================

void PlanningStrategy::configure_for_player_count(int num_players) {
    if (num_players <= 2) {
        min_energy_      = MIN_ENERGY_2P;
        min_cargo_       = MIN_CARGO_2P;
        time_penalty_    = TIME_PENALTY_2P;
        enemy_adj_pen_   = ENEMY_ADJACENT_PENALTY_2P;
        inspired_factor_ = INSPIRED_FACTOR_2P;
        min_attraction_  = MIN_ATTRACTION_2P;
    } else {
        min_energy_      = MIN_ENERGY_4P;
        min_cargo_       = MIN_CARGO_4P;
        time_penalty_    = TIME_PENALTY_4P;
        enemy_adj_pen_   = ENEMY_ADJACENT_PENALTY_4P;
        inspired_factor_ = INSPIRED_FACTOR_4P;
        min_attraction_  = MIN_ATTRACTION_4P;
    }
}

// ============================================================================
// Adaptation des seuils selon la densité de halite
// ============================================================================

void PlanningStrategy::adapt_thresholds(const Game& game) {
    int w = game.game_map->width;
    int h = game.game_map->height;
    int total = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            total += game.game_map->cells[y][x].halite;

    float avg = static_cast<float>(total) / static_cast<float>(w * h);

    // Seuils de retour adaptatifs :
    //   Carte riche (avg ~200+) → return à 600-700, forcer à ~400, last resort ~500
    //   Carte pauvre (avg ~50)  → return à 150, forcer à ~100, last resort ~125
    return_cargo_threshold_ = std::max(150, std::min(700, static_cast<int>(avg * 3.0f)));
    force_return_cargo_     = std::max(80,  static_cast<int>(return_cargo_threshold_ * 0.66f));
    last_resort_cargo_      = std::max(100, static_cast<int>(return_cargo_threshold_ * 0.83f));

    // Seuils de minage adaptatifs
    int base_energy = (game.players.size() <= 2) ? MIN_ENERGY_2P : MIN_ENERGY_4P;
    // Augmenter l'exigence cargo pour le minage local afin de favoriser le global/clusters
    // Si le local ne rapporte pas assez, on va chercher plus loin via global mining.
    int base_cargo  = (game.players.size() <= 2) ? MIN_CARGO_2P * 2 : MIN_CARGO_4P * 2;
    float base_attr = (game.players.size() <= 2) ? MIN_ATTRACTION_2P : MIN_ATTRACTION_4P;

    min_energy_     = std::max(5,    std::min(base_energy, static_cast<int>(avg * 0.3f)));
    min_cargo_      = std::max(50,   std::min(base_cargo,  static_cast<int>(avg * 2.5f)));
    min_attraction_ = std::max(20.0f, std::min(base_attr,  avg * 1.5f));

    log::log("Adaptive: avg=" + std::to_string(static_cast<int>(avg)) +
             " ret=" + std::to_string(return_cargo_threshold_) +
             " force=" + std::to_string(force_return_cargo_) +
             " last=" + std::to_string(last_resort_cargo_) +
             " minE=" + std::to_string(min_energy_) +
             " minC=" + std::to_string(min_cargo_) +
             " minA=" + std::to_string(static_cast<int>(min_attraction_)));
}

// ============================================================================
// Nettoyage des ships disparus
// ============================================================================

void PlanningStrategy::cleanup_dead_ships(const Game& game) {
    std::vector<EntityId> to_remove;
    for (const auto& [id, state] : ship_states_) {
        if (game.me->ships.find(id) == game.me->ships.end()) {
            to_remove.push_back(id);
        }
    }
    for (auto id : to_remove) {
        ship_states_.erase(id);
    }
}

// ============================================================================
// Coût de traversée Dijkstra
// ============================================================================

float PlanningStrategy::cell_traversal_cost(
    const Position& pos, const AnnotationMap& ann_map,
    const Game& game, bool is_delivery) const
{
    /*
     * Le coût de traversée est composé de :
     *   - Coût de base : 1 tour
     *   - Halite brûlé normalisé : halite_cell / MOVE_COST_RATIO / 1000
     *   - Zone hostile (dominance ≤ −5) : |dominance / 5| × 3.0
     *   - Ennemi adjacent : pénalité configurable (4p vs 2p)
     *   - Passage sur un dropoff allié hors livraison : +3.0
     */
    const CellInfo& info = ann_map.at(pos);
    const MapCell* cell = game.game_map->at(pos);

    float cost = 1.0f;

    // Pénalité halite brûlé — plus élevée pour les chemins de livraison
    // (les ships en retour perdent du cargo en traversant des cases riches)
    float burn_divisor = is_delivery ? 200.0f : 1000.0f;
    cost += static_cast<float>(cell->halite) /
            static_cast<float>(constants::MOVE_COST_RATIO) / burn_divisor;

    // Pénalité zone ennemie
    if (info.dominance_score <= -5) {
        cost += static_cast<float>(-info.dominance_score) / 5.0f * 3.0f;
    }

    // Pénalité ennemi adjacent
    if (info.enemy_reachable) {
        cost += enemy_adj_pen_;
    }

    // Pénalité dropoff allié traversé (hors livraison)
    if (!is_delivery && cell->has_structure()) {
        if (cell->structure && cell->structure->owner == game.my_id) {
            cost += DROPOFF_PENALTY;
        }
    }

    return cost;
}

// ============================================================================
// Dijkstra vers le dropoff le plus proche
// ============================================================================

std::pair<std::vector<Position>, float> PlanningStrategy::dijkstra_to_dropoff(
    const Position& start, int cargo,
    const AnnotationMap& ann_map, const Game& game, int max_nodes)
{
    /*
     * Dijkstra partant d'un ship, s'arrêtant dès qu'un dropoff/shipyard allié
     * est atteint. Le coût intègre les pénalités de cell_traversal_cost.
     *
     * Si le ship n'a pas assez de halite pour bouger (cargo < cell_halite / MOVE_COST_RATIO),
     * il doit d'abord miner sur place (le coût augmente de 1 par tour d'attente).
     */
    struct NodeInfo {
        float cost       = std::numeric_limits<float>::max();
        Position parent  = {-1, -1};
        bool visited     = false;
    };

    int w = ann_map.width();
    int h = ann_map.height();
    std::vector<std::vector<NodeInfo>> info(h, std::vector<NodeInfo>(w));

    auto norm = [w, h](const Position& p) -> Position {
        return {((p.x % w) + w) % w, ((p.y % h) + h) % h};
    };

    Position n_start = norm(start);
    info[n_start.y][n_start.x].cost = 0.0f;

    std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<>> pq;
    pq.push({n_start, 0.0f, 0});

    int nodes_explored = 0;
    Position best_dropoff{-1, -1};
    float best_cost = std::numeric_limits<float>::max();

    while (!pq.empty() && nodes_explored < max_nodes) {
        auto [pos, cost, turns] = pq.top(); pq.pop();

        Position np = norm(pos);
        if (info[np.y][np.x].visited) continue;
        info[np.y][np.x].visited = true;
        ++nodes_explored;

        // Vérifier si c'est un dropoff/shipyard allié (sauf la case de départ)
        if (np != n_start) {
            const MapCell* mc = game.game_map->at(np);
            if (mc->has_structure() && mc->structure->owner == game.my_id) {
                if (cost < best_cost) {
                    best_cost = cost;
                    best_dropoff = np;
                }
                continue; // Pas besoin d'explorer au-delà
            }
        }

        // Explorer les voisins
        for (auto d : ALL_CARDINALS) {
            Position neighbor = norm(np.directional_offset(d));
            if (info[neighbor.y][neighbor.x].visited) continue;

            float move_cost = cell_traversal_cost(neighbor, ann_map, game, true);
            float new_cost = cost + move_cost;

            if (new_cost < info[neighbor.y][neighbor.x].cost) {
                info[neighbor.y][neighbor.x].cost = new_cost;
                info[neighbor.y][neighbor.x].parent = np;
                pq.push({neighbor, new_cost, turns + 1});
            }
        }
    }

    // Reconstruire le chemin
    std::vector<Position> path;
    if (best_dropoff.x != -1) {
        Position current = best_dropoff;
        while (current != n_start) {
            path.push_back(current);
            current = info[current.y][current.x].parent;
            if (current.x == -1) { path.clear(); break; }
        }
        std::reverse(path.begin(), path.end());
    }

    return {path, best_cost};
}

// ============================================================================
// Direction extraite du premier pas d'un chemin
// ============================================================================

Direction PlanningStrategy::direction_from_path(
    const Position& from, const Position& to, const Game& game) const
{
    if (from == to) return Direction::STILL;

    auto moves = game.game_map->get_unsafe_moves(from, to);
    if (!moves.empty()) return moves[0];
    return Direction::STILL;
}

// ============================================================================
// Rôle 1 — End-game return
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_endgame_return(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    std::vector<Candidate> candidates;
    int turns_remaining = constants::MAX_TURNS - game.turn_number;

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;

        int dist = ann_map.at(ship->position).dist_to_dropoff;

        // Condition : pas assez de temps pour revenir avec la marge
        if (turns_remaining <= dist + ENDGAME_BUFFER_TURNS) {
            auto [path, cost] = dijkstra_to_dropoff(
                ship->position, ship->halite, ann_map, game, MAX_NODES_PATHFINDING);

            if (!path.empty()) {
                // Score inversé : le plus court trajet a le meilleur score
                float score = 10000.0f - cost;
                candidates.push_back({ship_id, path, score});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// ============================================================================
// Rôle 2 — Returning (livraison standard)
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_returning(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    std::vector<Candidate> candidates;

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;

        auto& state = ship_states_[ship_id];

        // Activer le retour si cargo suffisant
        if (ship->halite >= return_cargo_threshold_) {
            state.is_returning = true;
            state.target = {-1, -1};
            state.target_age = 0;
        }

        // Désactiver si cargo à zéro (livraison terminée)
        if (ship->halite == 0) {
            state.is_returning = false;
        }

        if (!state.is_returning) continue;

        auto [path, cost] = dijkstra_to_dropoff(
            ship->position, ship->halite, ann_map, game, MAX_NODES_PATHFINDING);

        if (!path.empty()) {
            float score = 10000.0f - cost;
            candidates.push_back({ship_id, path, score});
        }
    }

    std::sort(candidates.begin(), candidates.end());

    // Limiter les retours simultanés pour réduire la congestion aux dropoffs.
    // Seuls les MAX_RETURNING_PER_DROPOFF × N ships les plus proches retournent ;
    // les autres reprennent le minage local en attendant qu'un créneau se libère.
    int num_dropoff_pts = 1 + static_cast<int>(game.me->dropoffs.size());
    int max_returning = MAX_RETURNING_PER_DROPOFF * num_dropoff_pts;
    if (static_cast<int>(candidates.size()) > max_returning) {
        for (size_t i = max_returning; i < candidates.size(); ++i) {
            ship_states_[candidates[i].ship_id].is_returning = false;
        }
        candidates.resize(max_returning);
    }

    return candidates;
}

// ============================================================================
// Rôle 3 — Minage local (DFS courte portée)
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_local_mining(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    /*
     * DFS explorant toutes les cases à ≤ LOCAL_MINING_MAX_DEPTH de distance
     * du ship. Sur chaque case, simule le minage (25% par tour, avec bonus
     * inspiration) jusqu'à ce que le halite tombe sous min_energy_.
     *
     * Score = cargo_collecté - halite_brûlé - (coût_chemin + dist_retour) × time_penalty
     *
     * Le meilleur chemin est retenu comme candidat si le cargo dépasse min_cargo_.
     */
    std::vector<Candidate> candidates;
    int w = ann_map.width();
    int h = ann_map.height();

    auto norm = [w, h](const Position& p) -> Position {
        return {((p.x % w) + w) % w, ((p.y % h) + h) % h};
    };

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;
        if (ship_states_[ship_id].is_returning) continue;

        float best_score = -std::numeric_limits<float>::max();
        std::vector<Position> best_path;

        // État DFS : position, profondeur, cargo accumulé, halite brûlé, chemin parcouru
        struct DFSState {
            Position pos;
            int depth;
            int cargo;
            int burned;
            std::vector<Position> path;
        };

        std::vector<DFSState> stack;
        stack.push_back({ship->position, 0, ship->halite, 0, {}});

        while (!stack.empty()) {
            DFSState state = stack.back();
            stack.pop_back();

            Position np = norm(state.pos);

            // Simuler le minage sur la case courante
            int cell_halite = game.game_map->at(np)->halite;
            bool inspired = ann_map.at(np).is_inspired;
            int sim_cargo = state.cargo;
            int sim_burned = state.burned;
            std::vector<Position> sim_path = state.path;

            // Miner jusqu'à min_energy ou cargo plein
            while (cell_halite > min_energy_ && sim_cargo < constants::MAX_HALITE) {
                int extracted = cell_halite / constants::EXTRACT_RATIO;
                if (extracted == 0) extracted = 1;

                int bonus = 0;
                if (inspired && constants::INSPIRATION_ENABLED) {
                    bonus = static_cast<int>(extracted * constants::INSPIRED_BONUS_MULTIPLIER);
                }

                int total = std::min(extracted + bonus, constants::MAX_HALITE - sim_cargo);
                sim_cargo += total;
                cell_halite -= extracted;
                sim_path.push_back(np); // Un tour de minage = rester sur place
            }

            // Évaluer le score de ce chemin si le cargo est suffisant
            // Note : dist_return n'est PAS inclus dans le score. La décision de
            // retour est gérée par assign_returning (cargo ≥ RETURN_CARGO_THRESHOLD).
            // Inclure dist_return ici rendait le minage non-rentable dès que le ship
            // s'éloigne du dropoff, causant l'abandon du minage après un seul tour.
            if (sim_cargo >= min_cargo_ && !sim_path.empty()) {
                float path_cost = static_cast<float>(sim_path.size());
                float score = static_cast<float>(sim_cargo - ship->halite - sim_burned)
                            - path_cost * time_penalty_;

                if (score > best_score) {
                    best_score = score;
                    best_path = sim_path;
                }
            }

            // Explorer les voisins si la profondeur le permet
            if (state.depth < LOCAL_MINING_MAX_DEPTH) {
                for (auto d : ALL_CARDINALS) {
                    Position neighbor = norm(np.directional_offset(d));

                    // Éviter les cases marquées pour le minage par d'autres ships
                    if (ann_map.at(neighbor).marked_for_mining) continue;

                    /*
                     * Le coût de déplacement est basé sur le halite RESTANT sur
                     * la case APRÈS le minage (cell_halite post-simulation), et
                     * le cargo disponible est le sim_cargo (post-minage).
                     * Cela permet au ship de miner d'abord, puis de bouger.
                     */
                    int move_halite = cell_halite / constants::MOVE_COST_RATIO;
                    int new_cargo = sim_cargo - move_halite;
                    if (new_cargo < 0) continue; // Pas assez de halite pour bouger

                    std::vector<Position> new_path = sim_path;
                    new_path.push_back(neighbor);

                    stack.push_back({neighbor, state.depth + 1,
                                     new_cargo, sim_burned + move_halite, new_path});
                }
            }
        }

        if (!best_path.empty()) {
            candidates.push_back({ship_id, best_path, best_score});
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// ============================================================================
// Rôle 4 — Minage global (Dijkstra sur le champ d'attraction)
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_global_mining(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    /*
     * Dijkstra depuis chaque ship, explorant jusqu'à MAX_NODES_GLOBAL nœuds.
     * Chaque case atteinte dont l'attraction ≥ min_attraction_ et non réservée
     * pour minage est évaluée : score = attraction − coût × time_penalty.
     * La case avec le meilleur score est retenue.
     */
    std::vector<Candidate> candidates;
    int w = ann_map.width();
    int h = ann_map.height();

    auto norm = [w, h](const Position& p) -> Position {
        return {((p.x % w) + w) % w, ((p.y % h) + h) % h};
    };

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;
        if (ship_states_[ship_id].is_returning) continue;
        if (ship->halite >= last_resort_cargo_) continue; // Déjà quasi plein

        auto& state = ship_states_[ship_id];
        Position n_start = norm(ship->position);

        // -----------------------------------------------------------
        // Cible persistante : si le ship a déjà une cible valide,
        // naviguer directement vers elle sans recalculer Dijkstra.
        // La cible est invalidée si : trop vieille, ship arrivé, ou
        // la case ne vaut plus le voyage.
        // -----------------------------------------------------------
        bool has_persistent_target = false;
        if (state.target.x != -1 && state.target_age < MAX_TARGET_AGE) {
            Position nt = norm(state.target);
            if (nt == n_start) {
                // Arrivé à destination — effacer la cible et passer au minage local
                state.target = {-1, -1};
                state.target_age = 0;
            } else {
                const CellInfo& ci = ann_map.at(nt);
                if (ci.attraction >= min_attraction_ * 0.5f && !ci.marked_for_mining) {
                    // Cible toujours valide — naviguer vers elle
                    auto nav_moves = game.game_map->get_unsafe_moves(ship->position, nt);
                    if (!nav_moves.empty()) {
                        Position next = norm(ship->position.directional_offset(nav_moves[0]));
                        std::vector<Position> path_to_target = {next, nt};
                        float score = ci.attraction;
                        candidates.push_back({ship_id, path_to_target, score});
                        state.target_age++;
                        has_persistent_target = true;
                    }
                } else {
                    // Cible devenue invalide (déjà réservée ou appauvrie)
                    state.target = {-1, -1};
                    state.target_age = 0;
                }
            }
        }

        if (has_persistent_target) continue;

        // -----------------------------------------------------------
        // Pas de cible persistante — lancer Dijkstra pour en trouver une
        // -----------------------------------------------------------
        struct NodeInfo {
            float cost       = std::numeric_limits<float>::max();
            Position parent  = {-1, -1};
            bool visited     = false;
        };

        std::vector<std::vector<NodeInfo>> node_info(h, std::vector<NodeInfo>(w));
        node_info[n_start.y][n_start.x].cost = 0.0f;

        std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<>> pq;
        pq.push({n_start, 0.0f, 0});

        float best_score = -std::numeric_limits<float>::max();
        Position best_target{-1, -1};
        int nodes_explored = 0;

        while (!pq.empty() && nodes_explored < MAX_NODES_GLOBAL) {
            auto [pos, cost, turns] = pq.top(); pq.pop();

            Position np = norm(pos);
            if (node_info[np.y][np.x].visited) continue;
            node_info[np.y][np.x].visited = true;
            ++nodes_explored;

            // Évaluer cette case comme destination de minage
            const CellInfo& ci = ann_map.at(np);
            if (ci.attraction >= min_attraction_ && !ci.marked_for_mining && np != n_start) {
                float score = ci.attraction - cost * time_penalty_;
                if (score > best_score) {
                    best_score = score;
                    best_target = np;
                }
            }

            // Explorer les voisins
            for (auto d : ALL_CARDINALS) {
                Position neighbor = norm(np.directional_offset(d));
                if (node_info[neighbor.y][neighbor.x].visited) continue;

                float move_cost = cell_traversal_cost(neighbor, ann_map, game, false);
                float new_cost = cost + move_cost;

                if (new_cost < node_info[neighbor.y][neighbor.x].cost) {
                    node_info[neighbor.y][neighbor.x].cost = new_cost;
                    node_info[neighbor.y][neighbor.x].parent = np;
                    pq.push({neighbor, new_cost, turns + 1});
                }
            }
        }

        // Reconstruire le chemin vers la meilleure cible et enregistrer la cible persistante
        if (best_target.x != -1) {
            std::vector<Position> path;
            Position current = best_target;
            while (current != n_start) {
                path.push_back(current);
                current = node_info[current.y][current.x].parent;
                if (current.x == -1) { path.clear(); break; }
            }
            std::reverse(path.begin(), path.end());

            if (!path.empty()) {
                // Ajouter un tour de minage à la destination (path marking)
                path.push_back(best_target);
                candidates.push_back({ship_id, path, best_score});
                // Enregistrer la cible persistante
                state.target = best_target;
                state.target_age = 0;
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// ============================================================================
// Rôle 5 — Minage greedy (fallback avec bonus attaque)
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_greedy_mining(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    /*
     * Dijkstra avec scoring différent :
     *   score = min(halite_cell × bonus, (MAX_CARGO − cargo) × 4) / (coût + 1)
     *
     * Si la case contient un ennemi et la dominance est favorable (≥ seuil),
     * la valeur du cargo ennemi est ajoutée au halite de la case.
     * Cela encourage les collisions quand il n'y a rien de mieux à faire.
     */
    std::vector<Candidate> candidates;
    int w = ann_map.width();
    int h = ann_map.height();

    auto norm = [w, h](const Position& p) -> Position {
        return {((p.x % w) + w) % w, ((p.y % h) + h) % h};
    };

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;
        if (ship_states_[ship_id].is_returning) continue;

        struct NodeInfo {
            float cost       = std::numeric_limits<float>::max();
            Position parent  = {-1, -1};
            bool visited     = false;
        };

        std::vector<std::vector<NodeInfo>> node_info(h, std::vector<NodeInfo>(w));
        Position n_start = norm(ship->position);
        node_info[n_start.y][n_start.x].cost = 0.0f;

        std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<>> pq;
        pq.push({n_start, 0.0f, 0});

        float best_score = -std::numeric_limits<float>::max();
        Position best_target{-1, -1};
        int nodes_explored = 0;

        while (!pq.empty() && nodes_explored < MAX_NODES_GREEDY) {
            auto [pos, cost, turns] = pq.top(); pq.pop();

            Position np = norm(pos);
            if (node_info[np.y][np.x].visited) continue;
            node_info[np.y][np.x].visited = true;
            ++nodes_explored;

            if (np != n_start) {
                const CellInfo& ci = ann_map.at(np);
                const MapCell* mc = game.game_map->at(np);
                float energy = static_cast<float>(mc->halite);

                // Bonus inspiration
                if (ci.is_inspired) {
                    energy *= inspired_factor_;
                }

                // Bonus attaque : si un ennemi est présent et dominance favorable
                if (ci.has_enemy_ship && ci.dominance_score >= -2) {
                    if (mc->ship && mc->ship->owner != game.my_id) {
                        energy += static_cast<float>(mc->ship->halite);
                    }
                }

                // Limiter à ce qu'il faut pour remplir le cargo
                float max_useful = static_cast<float>((constants::MAX_HALITE - ship->halite) * 4);
                energy = std::min(energy, max_useful);

                float score = energy / (cost + 1.0f);
                if (score > best_score) {
                    best_score = score;
                    best_target = np;
                }
            }

            // Explorer les voisins
            for (auto d : ALL_CARDINALS) {
                Position neighbor = norm(np.directional_offset(d));
                if (node_info[neighbor.y][neighbor.x].visited) continue;

                float move_cost = cell_traversal_cost(neighbor, ann_map, game, false);
                float new_cost = cost + move_cost;

                if (new_cost < node_info[neighbor.y][neighbor.x].cost) {
                    node_info[neighbor.y][neighbor.x].cost = new_cost;
                    node_info[neighbor.y][neighbor.x].parent = np;
                    pq.push({neighbor, new_cost, turns + 1});
                }
            }
        }

        // Reconstruire le chemin
        if (best_target.x != -1) {
            std::vector<Position> path;
            Position current = best_target;
            while (current != n_start) {
                path.push_back(current);
                current = node_info[current.y][current.x].parent;
                if (current.x == -1) { path.clear(); break; }
            }
            std::reverse(path.begin(), path.end());

            if (!path.empty()) {
                candidates.push_back({ship_id, path, best_score});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// ============================================================================
// Rôle 6 — Last resort
// ============================================================================

std::vector<Candidate> PlanningStrategy::assign_last_resort(
    const Game& game, const AnnotationMap& ann_map,
    const std::unordered_set<EntityId>& assigned)
{
    std::vector<Candidate> candidates;

    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;

        // Si cargo suffisant, forcer le retour
        if (ship->halite >= last_resort_cargo_) {
            ship_states_[ship_id].is_returning = true;
            auto [path, cost] = dijkstra_to_dropoff(
                ship->position, ship->halite, ann_map, game, MAX_NODES_PATHFINDING);
            if (!path.empty()) {
                float score = 10000.0f - cost;
                candidates.push_back({ship_id, path, score});
                continue;
            }
        }

        // Sinon, rester sur place (miner si possible)
        std::vector<Position> stay_path = {ship->position};
        candidates.push_back({ship_id, stay_path, -1.0f});
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// ============================================================================
// Pipeline principal de planification
// ============================================================================

std::unordered_map<EntityId, Direction> PlanningStrategy::plan_turn(
    const Game& game, AnnotationMap& ann_map)
{
    configure_for_player_count(static_cast<int>(game.players.size()));
    adapt_thresholds(game);
    cleanup_dead_ships(game);

    std::unordered_map<EntityId, Direction> moves;
    std::unordered_set<EntityId> assigned;

    /*
     * Fonction utilitaire : prend les candidats d'un rôle et assigne les chemins
     * de façon greedy. Chaque ship assigné est marqué et son chemin est enregistré
     * dans la carte annotée pour éviter les conflits.
     */
    auto process_candidates = [&](std::vector<Candidate>& cands) {
        for (auto& cand : cands) {
            if (assigned.count(cand.ship_id)) continue;

            auto ship_it = game.me->ships.find(cand.ship_id);
            if (ship_it == game.me->ships.end()) continue;

            const auto& ship = ship_it->second;

            // Déterminer la direction à partir du premier pas du chemin
            Direction dir = Direction::STILL;
            if (!cand.path.empty()) {
                // Vérifier si le ship peut bouger (assez de cargo pour le déplacement)
                Position first_step = cand.path[0];
                if (first_step == ship->position) {
                    dir = Direction::STILL;
                } else {
                    int move_cost = game.game_map->at(ship->position)->halite
                                  / constants::MOVE_COST_RATIO;
                    if (ship->halite >= move_cost) {
                        dir = direction_from_path(ship->position, first_step, game);
                    } else {
                        dir = Direction::STILL; // Pas assez de halite, doit miner d'abord
                    }
                }
            }

            moves[cand.ship_id] = dir;
            assigned.insert(cand.ship_id);

            // Marquer le chemin dans l'annotation map
            bool mark_mining = !ship_states_[cand.ship_id].is_returning;
            ann_map.mark_path(cand.path, 0, mark_mining);
        }
    };

    // Traitement séquentiel des rôles par priorité décroissante
    auto endgame = assign_endgame_return(game, ann_map, assigned);
    process_candidates(endgame);

    auto returning = assign_returning(game, ann_map, assigned);
    process_candidates(returning);

    auto local_mining = assign_local_mining(game, ann_map, assigned);
    process_candidates(local_mining);

    // Force-return : ships avec cargo décent mais sans bon chemin de minage local
    // doivent rentrer plutôt que de vagabonder via le minage global/greedy.
    for (const auto& [ship_id, ship] : game.me->ships) {
        if (assigned.count(ship_id)) continue;
        if (ship_states_[ship_id].is_returning) continue;
        if (ship->halite >= force_return_cargo_) {
            ship_states_[ship_id].is_returning = true;
            ship_states_[ship_id].target = {-1, -1};
            ship_states_[ship_id].target_age = 0;
        }
    }

    // Re-traiter returning pour les ships qui viennent d'être forcés
    auto forced_returning = assign_returning(game, ann_map, assigned);
    process_candidates(forced_returning);

    auto global_mining = assign_global_mining(game, ann_map, assigned);
    process_candidates(global_mining);

    auto greedy = assign_greedy_mining(game, ann_map, assigned);
    process_candidates(greedy);

    auto last_resort = assign_last_resort(game, ann_map, assigned);
    process_candidates(last_resort);

    return moves;
}

} // namespace hlt
