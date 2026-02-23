#pragma once

/**
 * @file collision_policy.hpp
 * @brief Résolution anti-collision — safety net après la planification.
 *
 * Ce module vérifie que les mouvements planifiés ne provoquent pas de
 * collisions entre ships alliés. Il détecte les conflits de destination
 * (deux ships visant la même case) et les swaps (A→B, B→A), puis résout
 * chaque conflit en forçant un des ships à rester sur place.
 *
 * Exception end-game : les collisions sur les dropoffs alliés sont autorisées
 * dans les 20 derniers tours pour maximiser le dépôt.
 *
 * Module réutilisable : ne dépend que de hlt::Game et de la carte de mouvements.
 */

#include <unordered_map>

#include "game.hpp"
#include "types.hpp"
#include "direction.hpp"
#include "position.hpp"
#include "constants.hpp"

namespace hlt {

/// Nombre de tours avant la fin où les collisions sur dropoffs sont autorisées
constexpr int ENDGAME_COLLISION_TURNS = 30;

/**
 * @brief Politique de résolution des collisions entre ships alliés.
 *
 * Prend en entrée une carte de mouvements planifiés (ship_id → direction)
 * et la corrige pour éliminer les auto-collisions, sauf au end-game
 * sur les dropoffs alliés.
 */
class CollisionPolicy {
public:
    /**
     * @brief Résout les collisions dans la carte de mouvements.
     * @param game     État de jeu courant.
     * @param moves    Carte des mouvements planifiés (modifiée en place).
     * @param spawning True si un ship doit spawner (le shipyard doit rester vide à la fin du tour).
     *
     * Détecte et corrige :
     *   - Conflits de destination : 2 ships → même case → cargo min reste STILL
     *   - Swaps : A→B et B→A → un des deux reste STILL
     *
     * Exception : si tours_restants < ENDGAME_COLLISION_TURNS, les collisions
     * sur les dropoffs/shipyard alliés sont autorisées (sauf si spawn=true).
     */
    void resolve_collisions(const Game& game,
                            std::unordered_map<EntityId, Direction>& moves,
                            bool spawning = false) const;

private:
    /**
     * @brief Vérifie si une position est un dropoff/shipyard allié.
     * @param game État de jeu.
     * @param pos  Position à vérifier.
     * @return True si la position est un point de livraison allié.
     */
    bool is_allied_dropoff(const Game& game, const Position& pos) const;
};

} // namespace hlt
