#pragma once

#include "types.hpp"
#include "position.hpp"
#include "direction.hpp"
#include "game_map.hpp"
#include "player.hpp"
#include "annotation_map.hpp"
#include "ship.hpp"

#include <vector>
#include <unordered_map>
#include <memory>

namespace hlt {

    /**
     * @brief Represents a planned path for a ship with associated metadata.
     */
    struct PlannedPath {
        std::vector<Position> positions;  ///< Sequence of positions in the path
        std::vector<Direction> moves;     ///< Sequence of moves to execute
        float score;                      ///< Score of this path (higher is better)
        bool should_mine_at_end;          ///< Whether ship should mine at destination
        int estimated_turns;              ///< Estimated turns to complete path
        Halite estimated_cargo;           ///< Estimated cargo at end of path
        Halite halite_cost;               ///< Total halite cost for movement

        PlannedPath() : score(0.0f), should_mine_at_end(false), estimated_turns(0),
            estimated_cargo(0), halite_cost(0) {
        }
    };

    /**
     * @brief Role types for ship planning strategy.
     */
    enum class ShipRole {
        END_GAME_RETURN,    ///< Returning to dropoff before game ends
        RETURNING,          ///< Returning with full cargo
        LOCAL_MINING,       ///< Mining in local area
        GLOBAL_MINING,      ///< Seeking distant mining zones
        GREEDY_MINING,      ///< Aggressive/inspired mining
        LAST_RESORT         ///< Default behavior when no role fits
    };

    /**
     * @brief Result of pathfinding operation (C++14 alternative to std::optional).
     */
    struct PathfindResult {
        bool found;
        PlannedPath path;

        PathfindResult() : found(false) {}
        PathfindResult(const PlannedPath& p) : found(true), path(p) {}
    };

    /**
     * @brief Manages strategic planning for all ships each turn.
     *
     * The PlanningStrategy assigns roles, objectives, and paths to ships.
     * It processes ships by priority and uses greedy assignment to avoid conflicts.
     */
    class PlanningStrategy {
    public:
        /**
         * @brief Ship state that persists across turns.
         */
        struct ShipState {
            bool is_returning;              ///< True if ship is in returning state
            std::vector<Position> current_path; ///< Current planned path
            ShipRole assigned_role;         ///< Current role assignment

            ShipState() : is_returning(false), assigned_role(ShipRole::LAST_RESORT) {}
        };

        /**
         * @brief Constructs a planning strategy manager.
         */
        PlanningStrategy();

        /**
         * @brief Plans actions for all ships for the current turn.
         * @param game_map Current game map
         * @param annotation_map Precomputed annotation map
         * @param me Current player
         * @param players All players
         * @param turn_number Current turn number
         * @param num_players Total number of players
         * @return Map of ship ID to planned direction
         */
        std::unordered_map<EntityId, Direction> plan_turn(
            GameMap& game_map,
            AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            const std::vector<std::shared_ptr<Player>>& players,
            int turn_number,
            int num_players
        );

        /**
         * @brief Gets persistent state for a specific ship.
         * @param ship_id Ship entity ID
         * @return Reference to ship state
         */
        ShipState& get_ship_state(EntityId ship_id);

    private:
        std::unordered_map<EntityId, ShipState> ship_states_; ///< Persistent ship states

        /**
         * @brief Assigns role and generates candidate paths for a ship.
         */
        std::vector<PlannedPath> generate_candidate_paths(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            const std::vector<std::shared_ptr<Player>>& players,
            int turn_number,
            int num_players
        );

        /**
         * @brief Plans end-game return to dropoff.
         */
        std::vector<PlannedPath> plan_end_game_return(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            int turn_number
        );

        /**
         * @brief Plans return to dropoff for full ships.
         */
        std::vector<PlannedPath> plan_return(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me
        );

        /**
         * @brief Plans local mining within short distance.
         */
        std::vector<PlannedPath> plan_local_mining(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            int num_players
        );

        /**
         * @brief Plans global mining using attraction field.
         */
        std::vector<PlannedPath> plan_global_mining(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map
        );

        /**
         * @brief Plans greedy/aggressive mining with inspiration bonus.
         */
        std::vector<PlannedPath> plan_greedy_mining(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            int num_players
        );

        /**
         * @brief Plans last resort action (return or wait).
         */
        std::vector<PlannedPath> plan_last_resort(
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me
        );

        /**
         * @brief Finds path using Dijkstra with game-specific costs.
         */
        PathfindResult find_path_dijkstra(
            const Position& start,
            const Position& goal,
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            bool allow_waiting = true,
            bool is_end_game_return = false
        );

        /**
         * @brief Performs depth-first search for local mining.
         */
        std::vector<PlannedPath> depth_first_local_mining(
            const Position& start,
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            int max_dist,
            int min_energy,
            int min_cargo,
            float time_penalty
        );

        /**
         * @brief Calculates movement cost including penalties.
         */
        float calculate_move_cost(
            const Position& from,
            const Position& to,
            const std::shared_ptr<Ship>& ship,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            bool is_end_game_return = false
        );

        /**
         * @brief Checks if a path conflicts with already marked paths.
         */
        bool has_path_conflict(
            const PlannedPath& path,
            const AnnotationMap& annotation_map
        ) const;

        /**
         * @brief Simulates mining on a cell until threshold.
         */
        std::pair<Halite, int> simulate_mining(
            Halite cell_halite,
            Halite ship_cargo,
            int threshold,
            bool is_inspired
        ) const;

        /**
         * @brief Finds nearest dropoff position.
         */
        Position find_nearest_dropoff(
            const Position& pos,
            const std::shared_ptr<Player>& me,
            GameMap& game_map
        ) const;
    };

} // namespace hlt