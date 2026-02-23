/**
 * @file dropoff_policy.cpp
 * @brief Implémentation de la politique de construction de dropoffs.
 *
 * Évalue chaque case de la carte selon 5 critères :
 *   1. Halite disponible ≥ DROPOFF_COST
 *   2. Distance suffisante aux autres dropoffs/shipyard alliés
 *   3. Dominance ≥ seuil sur les grandes cartes
 *   4. Au moins N ships alliés dans un rayon donné
 *   5. Au moins M halite dans un rayon donné
 *
 * La case ayant le plus de halite dans son voisinage est retenue, et
 * le ship le plus proche est désigné pour la construction.
 */

#include "dropoff_policy.hpp"

#include <limits>

namespace hlt {

std::optional<DropoffDecision> DropoffPolicy::evaluate(
    const Game& game, const AnnotationMap& ann_map) const
{
    int w = game.game_map->width;
    int h = game.game_map->height;

    // Pas de dropoff sur les petites cartes (32×32) — pas rentable
    if (w < 40) return std::nullopt;
    
    // Pas de dropoff proche de la fin de partie (ROI impossible)
    if (game.turn_number > constants::MAX_TURNS - 100) return std::nullopt;

    // Limite globale du nombre de dropoffs (Arbitrage Ship/Dropoff)
    // Règle heuristique : 1 dropoff pour 15 ships.
    // En dessous de ce ratio, il vaut mieux produire des ships.
    size_t max_dropoffs = game.me->ships.size() / 15;
    if (game.me->dropoffs.size() >= max_dropoffs) return std::nullopt;

    int min_dist = min_dropoff_distance(w);

    Position best_pos{-1, -1};
    int best_halite = -1;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Position pos(x, y);
            const CellInfo& ci = ann_map.at(pos);

            // Condition 1 : la case ne doit pas déjà contenir une structure
            const MapCell* mc = game.game_map->at(pos);
            if (mc->has_structure()) continue;

            // Protection supplémentaire : vérifier explicitement les dropoffs/shipyard
            if (pos == game.me->shipyard->position) continue;
            bool structure_exists = false;
            for (const auto& [did, d] : game.me->dropoffs) {
                if (d->position == pos) { structure_exists = true; break; }
            }
            if (structure_exists) continue;

            // Condition 2 : distance aux dropoffs/shipyard existants
            if (ci.dist_to_dropoff < min_dist) continue;

            // Condition 3 : dominance suffisante sur les grandes cartes
            if (w >= 56 && ci.dominance_score < DROPOFF_DOMINANCE_THRESHOLD) continue;

            // Condition 4 : assez de ships alliés à proximité
            int nearby_ships = allied_ships_in_radius(game, pos, DROPOFF_SHIP_RADIUS);
            if (nearby_ships < DROPOFF_MIN_NEARBY_SHIPS) continue;

            // Condition 5 : assez de halite dans le voisinage
            int nearby_halite = halite_in_radius(game, pos, DROPOFF_HALITE_RADIUS);
            if (nearby_halite < DROPOFF_MIN_HALITE_NEARBY) continue;

            // Sélection : case avec le plus de halite dans son rayon
            if (nearby_halite > best_halite) {
                best_halite = nearby_halite;
                best_pos = pos;
            }
        }
    }

    if (best_pos.x == -1) return std::nullopt;

    // Trouver le meilleur constructeur : ship avec le plus de cargo dans un
    // rayon raisonnable, pour minimiser le coût effectif du dropoff.
    EntityId best_builder = -1;
    int best_builder_cargo = -1;
    int best_builder_dist = std::numeric_limits<int>::max();

    for (const auto& [ship_id, ship] : game.me->ships) {
        int dist = game.game_map->calculate_distance(ship->position, best_pos);
        if (dist > 20) continue;
        if (ship->halite > best_builder_cargo ||
            (ship->halite == best_builder_cargo && dist < best_builder_dist)) {
            best_builder = ship_id;
            best_builder_cargo = ship->halite;
            best_builder_dist = dist;
        }
    }

    // Fallback : ship le plus proche quelle que soit sa distance
    if (best_builder == -1) {
        int fallback_dist = std::numeric_limits<int>::max();
        for (const auto& [ship_id, ship] : game.me->ships) {
            int dist = game.game_map->calculate_distance(ship->position, best_pos);
            if (dist < fallback_dist) {
                fallback_dist = dist;
                best_builder = ship_id;
                best_builder_cargo = ship->halite;
            }
        }
    }

    if (best_builder == -1) return std::nullopt;

    // Coût effectif : le cargo du ship et le halite de la case contribuent
    int cell_h = game.game_map->at(best_pos)->halite;
    int effective_cost = std::max(0, constants::DROPOFF_COST - best_builder_cargo - cell_h);

    if (game.me->halite < effective_cost) return std::nullopt;

    return DropoffDecision{best_pos, best_builder};
}

int DropoffPolicy::min_dropoff_distance(int map_width) const {
    if (map_width <= 32) return DROPOFF_MIN_DIST_32;
    if (map_width <= 48) return DROPOFF_MIN_DIST_48;
    return DROPOFF_MIN_DIST_64;
}

int DropoffPolicy::halite_in_radius(const Game& game, const Position& center,
                                    int radius) const
{
    /*
     * Parcourt toutes les cases dans un carré de côté (2×radius + 1) centré
     * sur la position. Seules les cases à distance Manhattan ≤ radius sont
     * comptées. La normalisation gère la toroïdalité de la carte.
     */
    int total = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::abs(dx) + std::abs(dy) > radius) continue;
            Position p = game.game_map->normalize({center.x + dx, center.y + dy});
            total += game.game_map->cells[p.y][p.x].halite;
        }
    }
    return total;
}

int DropoffPolicy::allied_ships_in_radius(const Game& game, const Position& center,
                                          int radius) const
{
    int count = 0;
    for (const auto& [ship_id, ship] : game.me->ships) {
        if (game.game_map->calculate_distance(ship->position, center) <= radius) {
            ++count;
        }
    }
    return count;
}

} // namespace hlt
