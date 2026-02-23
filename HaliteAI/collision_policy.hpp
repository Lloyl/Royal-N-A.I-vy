#pragma once

#include "types.hpp"
#include "position.hpp"
#include "direction.hpp"
#include "game_map.hpp"
#include "player.hpp"

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

namespace hlt {

    /**
     * @brief Manages collision avoidance and resolution.
     *
     * The CollisionPolicy ensures no friendly ships collide, except when:
     * - It's end-game and ships are depositing on dropoffs
     * - Intentional collisions are allowed (cycles)
     *
     * It detects conflicts and resolves them by forcing ships to wait
     * or by unwinding movement chains.
     */
    class CollisionPolicy {
    public:
        /**
         * @brief Constructs a collision policy manager.
         */
        CollisionPolicy();

        /**
         * @brief Resolves collisions in planned moves.
         * @param planned_moves Map of ship ID to intended direction
         * @param game_map Current game map
         * @param me Current player
         * @param turn_number Current turn number
         * @return Collision-free map of ship ID to direction
         */
        std::unordered_map<EntityId, Direction> resolve_collisions(
            const std::unordered_map<EntityId, Direction>& planned_moves,
            GameMap& game_map,
            const std::shared_ptr<Player>& me,
            int turn_number
        );

    private:
        /**
         * @brief Detects all collision conflicts in planned moves.
         */
        std::unordered_set<EntityId> detect_conflicts(
            const std::unordered_map<EntityId, Direction>& planned_moves,
            GameMap& game_map,
            const std::shared_ptr<Player>& me
        ) const;

        /**
         * @brief Checks if a position is a friendly dropoff.
         */
        bool is_dropoff_position(
            const Position& pos,
            const std::shared_ptr<Player>& me,
            GameMap& game_map
        ) const;

        /**
         * @brief Detects movement cycles (A->B, B->A).
         */
        bool has_cycles(
            const std::unordered_map<EntityId, Direction>& planned_moves,
            GameMap& game_map,
            const std::shared_ptr<Player>& me
        ) const;

        /**
         * @brief Resolves a specific collision by forcing a ship to wait.
         */
        void resolve_conflict(
            const std::unordered_set<EntityId>& conflicting_ships,
            std::unordered_map<EntityId, Direction>& planned_moves,
            GameMap& game_map,
            const std::shared_ptr<Player>& me
        );

        /**
         * @brief Builds a position-to-ship map for conflict detection.
         */
        std::unordered_map<Position, std::vector<EntityId>> build_destination_map(
            const std::unordered_map<EntityId, Direction>& planned_moves,
            GameMap& game_map,
            const std::shared_ptr<Player>& me
        ) const;
    };

} // namespace hlt