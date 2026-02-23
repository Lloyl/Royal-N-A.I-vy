/**
 * @file spawn_policy.cpp
 * @brief Implémentation de la politique de spawn.
 *
 * Règles de décision simples et efficaces pour le spawn :
 *   1. Halite disponible ≥ coût d'un ship
 *   2. Shipyard non occupé par un de nos ships
 *   3. Tour courant < MAX_TURNS − marge
 *   4. Halite restant sur la carte > seuil minimum
 *   5. Nombre de ships < cap selon la taille de carte
 */

#include "spawn_policy.hpp"

namespace hlt {

bool SpawnPolicy::should_spawn(const Game& game, const AnnotationMap& ann_map,
                               int initial_halite) const
{
    // Règle 1 : assez de halite en réserve pour construire un ship
    if (game.me->halite < constants::SHIP_COST) {
        return false;
    }

    // Règle 2 : le shipyard ne doit pas être occupé par un de nos ships
    const MapCell* shipyard_cell = game.game_map->at(game.me->shipyard);
    if (shipyard_cell->is_occupied()) {
        if (shipyard_cell->ship->owner == game.my_id) {
            return false;
        }
    }

    // Règle 3 : assez de tours restants pour rentabiliser le ship
    if (game.turn_number >= constants::MAX_TURNS - SPAWN_TURN_MARGIN) {
        return false;
    }

    // Règle 4 : assez de halite sur la carte pour que miner soit rentable
    int remaining_halite = total_map_halite(game);
    if (remaining_halite < static_cast<int>(initial_halite * MIN_HALITE_RATIO)) {
        return false;
    }

    // Règle 5 : ne pas dépasser le cap de ships pour cette taille de carte
    int current_ships = static_cast<int>(game.me->ships.size());
    int max_ships = ship_cap(game.game_map->width);
    if (current_ships >= max_ships) {
        return false;
    }

    // Règle 6 : assez de halite par ship pour rentabiliser un nouveau ship
    // (chaque ship doit pouvoir miner au moins 2× son coût)
    int num_players = static_cast<int>(game.players.size());
    int halite_per_ship = remaining_halite / ((current_ships + 1) * num_players);
    if (halite_per_ship < constants::SHIP_COST * 2) {
        return false;
    }

    return true;
}

int SpawnPolicy::ship_cap(int map_width) const {
    if (map_width <= 32) return SHIP_CAP_32;
    if (map_width <= 48) return SHIP_CAP_48;
    return SHIP_CAP_64;
}

int SpawnPolicy::total_map_halite(const Game& game) const {
    int total = 0;
    for (int y = 0; y < game.game_map->height; ++y) {
        for (int x = 0; x < game.game_map->width; ++x) {
            total += game.game_map->cells[y][x].halite;
        }
    }
    return total;
}

} // namespace hlt
