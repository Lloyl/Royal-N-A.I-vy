#pragma once

#include "types.hpp"
#include "position.hpp"
#include "game_map.hpp"
#include "player.hpp"
#include "annotation_map.hpp"

#include <memory>
#include <vector>

namespace hlt {

    /**
     * @brief Manages dropoff site placement decisions.
     *
     * The DropoffPolicy analyzes the map to determine optimal dropoff locations
     * based on:
     * - Distance from existing dropoffs
     * - Local dominance
     * - Ship activity in the area
     * - Halite density
     */
    class DropoffPolicy {
    public:
        /**
         * @brief Dropoff candidate with evaluation data.
         */
        struct DropoffCandidate {
            Position position;              ///< Proposed dropoff position
            float score;                    ///< Evaluation score
            int nearby_ships;               ///< Number of ships operating nearby
            Halite nearby_halite;           ///< Total halite in radius
            int dominance_score;            ///< Dominance at location

            DropoffCandidate() : score(0.0f), nearby_ships(0), nearby_halite(0), dominance_score(0) {}
        };

        /**
         * @brief Result of dropoff evaluation (C++14 alternative to std::optional).
         */
        struct DropoffResult {
            bool has_location;
            Position location;

            DropoffResult() : has_location(false) {}
            DropoffResult(const Position& pos) : has_location(true), location(pos) {}
        };

        /**
         * @brief Constructs a dropoff policy manager.
         */
        DropoffPolicy();

        /**
         * @brief Evaluates and potentially selects a dropoff location.
         * @param game_map Current game map
         * @param annotation_map Annotation map
         * @param me Current player
         * @param planned_moves Ship movements planned for this turn
         * @return Result with optional dropoff position
         */
        DropoffResult evaluate_dropoff_location(
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            const std::unordered_map<EntityId, Direction>& planned_moves
        );

        /**
         * @brief Checks if a dropoff is already planned.
         * @return True if dropoff location has been determined
         */
        bool has_planned_dropoff() const;

        /**
         * @brief Gets the planned dropoff position.
         * @return Position of planned dropoff
         */
        Position get_planned_dropoff() const;

        /**
         * @brief Sets a planned dropoff location.
         * @param pos Position for future dropoff
         */
        void set_planned_dropoff(const Position& pos);

        /**
         * @brief Clears the planned dropoff.
         */
        void clear_planned_dropoff();

        /**
         * @brief Finds the nearest ship to build the dropoff.
         * @param target_pos Dropoff target position
         * @param game_map Current game map
         * @param me Current player
         * @return Ship ID of nearest ship (or -1 if none found)
         */
        EntityId find_nearest_ship_for_dropoff(
            const Position& target_pos,
            GameMap& game_map,
            const std::shared_ptr<Player>& me
        ) const;

        /**
         * @brief Sets minimum distance from existing dropoffs.
         * @param dist Minimum distance (default varies by map size)
         */
        void set_min_distance(int dist);

        /**
         * @brief Sets minimum dominance threshold.
         * @param dominance Minimum dominance score (default -5)
         */
        void set_min_dominance(int dominance);

        /**
         * @brief Sets minimum ships in radius requirement.
         * @param count Minimum ship count
         */
        void set_min_ships_in_radius(int count);

        /**
         * @brief Sets radius for ship/halite evaluation.
         * @param radius Evaluation radius
         */
        void set_evaluation_radius(int radius);

        /**
         * @brief Sets minimum halite in radius requirement.
         * @param halite Minimum halite threshold
         */
        void set_min_halite_in_radius(Halite halite);

    private:
        bool has_planned_dropoff_;            ///< True if a dropoff is planned
        Position planned_dropoff_;            ///< Next planned dropoff position
        int min_distance_;                    ///< Minimum distance from existing dropoffs
        int min_dominance_;                   ///< Minimum dominance score
        int min_ships_in_radius_;             ///< Minimum ships needed nearby
        int evaluation_radius_;               ///< Radius for evaluation
        Halite min_halite_in_radius_;        ///< Minimum halite needed nearby

        /**
         * @brief Evaluates all potential dropoff locations.
         */
        std::vector<DropoffCandidate> evaluate_all_positions(
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            const std::unordered_map<EntityId, Direction>& planned_moves
        ) const;

        /**
         * @brief Checks if position meets minimum distance requirement.
         */
        bool meets_distance_requirement(
            const Position& pos,
            const std::shared_ptr<Player>& me,
            GameMap& game_map
        ) const;

        /**
         * @brief Counts ships operating in radius.
         */
        int count_ships_in_radius(
            const Position& pos,
            GameMap& game_map,
            const AnnotationMap& annotation_map,
            const std::shared_ptr<Player>& me,
            const std::unordered_map<EntityId, Direction>& planned_moves
        ) const;

        /**
         * @brief Calculates total halite in radius.
         */
        Halite calculate_halite_in_radius(
            const Position& pos,
            GameMap& game_map
        ) const;
    };

} // namespace hlt