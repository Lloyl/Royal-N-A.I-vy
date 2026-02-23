#include "dropoff_policy.hpp"
#include "constants.hpp"
#include <algorithm>
#include <cmath>

namespace hlt {

    DropoffPolicy::DropoffPolicy()
        : has_planned_dropoff_(false),
        min_distance_(10),
        min_dominance_(-5),
        min_ships_in_radius_(3),
        evaluation_radius_(10),
        min_halite_in_radius_(5000) {
    }

    DropoffPolicy::DropoffResult DropoffPolicy::evaluate_dropoff_location(
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        const std::unordered_map<EntityId, Direction>& planned_moves
    ) {
        // If already planned, return it
        if (has_planned_dropoff_) {
            return DropoffResult(planned_dropoff_);
        }

        // Evaluate all positions
        std::vector<DropoffCandidate> candidates = evaluate_all_positions(game_map, annotation_map, me, planned_moves);

        if (candidates.empty()) {
            return DropoffResult();
        }

        // Sort by score
        std::sort(candidates.begin(), candidates.end(),
            [](const DropoffCandidate& a, const DropoffCandidate& b) {
                return a.score > b.score;
            });

        // Return best candidate if it passes all thresholds
        const DropoffCandidate& best = candidates[0];
        if (best.nearby_ships >= min_ships_in_radius_ &&
            best.nearby_halite >= min_halite_in_radius_ &&
            best.dominance_score >= min_dominance_) {
            return DropoffResult(best.position);
        }

        return DropoffResult();
    }

    bool DropoffPolicy::has_planned_dropoff() const {
        return has_planned_dropoff_;
    }

    Position DropoffPolicy::get_planned_dropoff() const {
        return planned_dropoff_;
    }

    void DropoffPolicy::set_planned_dropoff(const Position& pos) {
        has_planned_dropoff_ = true;
        planned_dropoff_ = pos;
    }

    void DropoffPolicy::clear_planned_dropoff() {
        has_planned_dropoff_ = false;
    }

    EntityId DropoffPolicy::find_nearest_ship_for_dropoff(
        const Position& target_pos,
        GameMap& game_map,
        const std::shared_ptr<Player>& me
    ) const {
        EntityId nearest_id = -1;
        int min_distance = 999999;

        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            EntityId ship_id = it->first;
            const std::shared_ptr<Ship>& ship = it->second;
            int dist = game_map.calculate_distance(ship->position, target_pos);
            if (dist < min_distance) {
                min_distance = dist;
                nearest_id = ship_id;
            }
        }

        return nearest_id;
    }

    void DropoffPolicy::set_min_distance(int dist) {
        min_distance_ = dist;
    }

    void DropoffPolicy::set_min_dominance(int dominance) {
        min_dominance_ = dominance;
    }

    void DropoffPolicy::set_min_ships_in_radius(int count) {
        min_ships_in_radius_ = count;
    }

    void DropoffPolicy::set_evaluation_radius(int radius) {
        evaluation_radius_ = radius;
    }

    void DropoffPolicy::set_min_halite_in_radius(Halite halite) {
        min_halite_in_radius_ = halite;
    }

    std::vector<DropoffPolicy::DropoffCandidate> DropoffPolicy::evaluate_all_positions(
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        const std::unordered_map<EntityId, Direction>& planned_moves
    ) const {
        std::vector<DropoffCandidate> candidates;

        // Sample positions across the map
        int step = 3; // Check every 3rd cell

        for (int y = 0; y < game_map.height; y += step) {
            for (int x = 0; x < game_map.width; x += step) {
                Position pos(x, y);

                // Check distance requirement
                if (!meets_distance_requirement(pos, me, game_map)) {
                    continue;
                }

                // Get dominance
                const AnnotationMap::CellInfo& cell_info = annotation_map.at(pos);
                if (cell_info.dominance_score < min_dominance_) {
                    continue;
                }

                // Count ships and halite in radius
                int ship_count = count_ships_in_radius(pos, game_map, annotation_map, me, planned_moves);
                Halite halite_amount = calculate_halite_in_radius(pos, game_map);

                // Calculate score
                DropoffCandidate candidate;
                candidate.position = pos;
                candidate.nearby_ships = ship_count;
                candidate.nearby_halite = halite_amount;
                candidate.dominance_score = cell_info.dominance_score;

                // Score combines all factors
                candidate.score = static_cast<float>(ship_count) * 100.0f +
                    static_cast<float>(halite_amount) / 100.0f +
                    static_cast<float>(cell_info.dominance_score) * 10.0f;

                candidates.push_back(candidate);
            }
        }

        return candidates;
    }

    bool DropoffPolicy::meets_distance_requirement(
        const Position& pos,
        const std::shared_ptr<Player>& me,
        GameMap& game_map
    ) const {
        // Check distance to shipyard
        if (me->shipyard) {
            int dist = game_map.calculate_distance(pos, me->shipyard->position);
            if (dist < min_distance_) {
                return false;
            }
        }

        // Check distance to existing dropoffs
        for (auto it = me->dropoffs.begin(); it != me->dropoffs.end(); ++it) {
            const std::shared_ptr<Dropoff>& dropoff = it->second;
            int dist = game_map.calculate_distance(pos, dropoff->position);
            if (dist < min_distance_) {
                return false;
            }
        }

        return true;
    }

    int DropoffPolicy::count_ships_in_radius(
        const Position& pos,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        const std::unordered_map<EntityId, Direction>& planned_moves
    ) const {
        int count = 0;

        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            const std::shared_ptr<Ship>& ship = it->second;
            int dist = game_map.calculate_distance(ship->position, pos);

            if (dist <= evaluation_radius_) {
                count++;
            }
        }

        return count;
    }

    Halite DropoffPolicy::calculate_halite_in_radius(
        const Position& pos,
        GameMap& game_map
    ) const {
        Halite total = 0;

        for (int dy = -evaluation_radius_; dy <= evaluation_radius_; ++dy) {
            for (int dx = -evaluation_radius_; dx <= evaluation_radius_; ++dx) {
                int manhattan_dist = std::abs(dx) + std::abs(dy);
                if (manhattan_dist > evaluation_radius_) continue;

                int x = ((pos.x + dx) % game_map.width + game_map.width) % game_map.width;
                int y = ((pos.y + dy) % game_map.height + game_map.height) % game_map.height;

                total += game_map.at(Position(x, y))->halite;
            }
        }

        return total;
    }

} // namespace hlt