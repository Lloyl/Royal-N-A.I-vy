#pragma once

#include "types.hpp"
#include "game_map.hpp"
#include "player.hpp"
#include "annotation_map.hpp"

#include <memory>
#include <vector>

namespace hlt {

    /**
     * @brief Manages ship spawning decisions.
     *
     * The SpawnPolicy determines whether to spawn a new ship based on:
     * - Available halite
     * - Remaining turns
     * - Remaining halite on map
     * - Shipyard availability
     */
    class SpawnPolicy {
    public:
        /**
         * @brief Constructs a spawn policy manager.
         */
        SpawnPolicy();

        /**
         * @brief Decides whether to spawn a ship this turn.
         * @param game_map Current game map
         * @param annotation_map Annotation map
         * @param me Current player
         * @param turn_number Current turn number
         * @param initial_halite_on_map Total halite at game start
         * @return True if should spawn a ship
         */
        bool should_spawn(
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            int turn_number,
            int initial_halite_on_map
        );

        /**
         * @brief Sets minimum turns remaining threshold.
         * @param turns Minimum turns (default 200)
         */
        void set_min_turns_remaining(int turns);

        /**
         * @brief Sets minimum map halite percentage threshold.
         * @param percentage Minimum percentage (default 0.33 = 33%)
         */
        void set_min_map_halite_percentage(float percentage);

    private:
        int min_turns_remaining_;           ///< Minimum turns to allow spawning
        float min_map_halite_percentage_;   ///< Minimum halite percentage on map

        /**
         * @brief Calculates total halite remaining on map.
         * @param game_map Current game map
         * @return Total halite on all cells
         */
        int calculate_remaining_halite(GameMap& game_map) const;

        /**
         * @brief Checks if shipyard is blocked.
         * @param game_map Current game map
         * @param me Current player
         * @return True if shipyard has a ship on it
         */
        bool is_shipyard_blocked(GameMap& game_map, const std::shared_ptr<Player>& me) const;
    };

} // namespace hlt