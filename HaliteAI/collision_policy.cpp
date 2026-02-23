#include "collision_policy.hpp"
#include "constants.hpp"
#include <algorithm>

namespace hlt {

    CollisionPolicy::CollisionPolicy() {}

    std::unordered_map<EntityId, Direction> CollisionPolicy::resolve_collisions(
        const std::unordered_map<EntityId, Direction>& planned_moves,
        GameMap& game_map,
        const std::shared_ptr<Player>& me,
        int turn_number
    ) {
        std::unordered_map<EntityId, Direction> resolved_moves = planned_moves;

        // In end-game (last ~20 turns), allow collisions on dropoffs
        bool is_end_game = (turn_number >= constants::MAX_TURNS - 20);

        // Iteratively resolve conflicts
        int max_iterations = 10;
        for (int iter = 0; iter < max_iterations; ++iter) {
            std::unordered_set<EntityId> conflicts = detect_conflicts(resolved_moves, game_map, me);

            if (conflicts.empty()) {
                break; // No more conflicts
            }

            // Resolve conflicts
            resolve_conflict(conflicts, resolved_moves, game_map, me);
        }

        // Final pass: if end-game, allow collisions on dropoffs
        if (is_end_game) {
            std::unordered_map<Position, std::vector<EntityId>> dest_map = build_destination_map(resolved_moves, game_map, me);

            for (auto it = dest_map.begin(); it != dest_map.end(); ++it) {
                const Position& pos = it->first;
                const std::vector<EntityId>& ship_ids = it->second;

                if (ship_ids.size() > 1 && is_dropoff_position(pos, me, game_map)) {
                    // Allow collision on dropoff - all ships can move there
                    // No changes needed, they're already planned to go there
                }
            }
        }

        return resolved_moves;
    }

    std::unordered_set<EntityId> CollisionPolicy::detect_conflicts(
        const std::unordered_map<EntityId, Direction>& planned_moves,
        GameMap& game_map,
        const std::shared_ptr<Player>& me
    ) const {
        std::unordered_set<EntityId> conflicting_ships;

        // Build destination map
        std::unordered_map<Position, std::vector<EntityId>> dest_map = build_destination_map(planned_moves, game_map, me);

        // Find positions with multiple ships
        for (auto it = dest_map.begin(); it != dest_map.end(); ++it) {
            const std::vector<EntityId>& ship_ids = it->second;
            if (ship_ids.size() > 1) {
                // Multiple ships heading to same position
                for (size_t i = 0; i < ship_ids.size(); ++i) {
                    conflicting_ships.insert(ship_ids[i]);
                }
            }
        }

        return conflicting_ships;
    }

    bool CollisionPolicy::is_dropoff_position(
        const Position& pos,
        const std::shared_ptr<Player>& me,
        GameMap& game_map
    ) const {
        Position normalized = game_map.normalize(pos);

        // Check shipyard
        if (me->shipyard && me->shipyard->position == normalized) {
            return true;
        }

        // Check dropoffs
        for (auto it = me->dropoffs.begin(); it != me->dropoffs.end(); ++it) {
            const std::shared_ptr<Dropoff>& dropoff = it->second;
            if (dropoff->position == normalized) {
                return true;
            }
        }

        return false;
    }

    bool CollisionPolicy::has_cycles(
        const std::unordered_map<EntityId, Direction>& planned_moves,
        GameMap& game_map,
        const std::shared_ptr<Player>& me
    ) const {
        // Build position-to-ship map (current positions)
        std::unordered_map<Position, EntityId> current_positions;
        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            const std::shared_ptr<Ship>& ship = it->second;
            current_positions[ship->position] = ship->id;
        }

        // Check for 2-cycles (A->B, B->A)
        for (auto it = planned_moves.begin(); it != planned_moves.end(); ++it) {
            EntityId ship_id = it->first;
            Direction direction = it->second;

            if (direction == Direction::STILL) continue;

            std::shared_ptr<Ship> ship = me->ships.at(ship_id);
            Position current = ship->position;
            Position next = game_map.normalize(current.directional_offset(direction));

            // Check if there's a ship at destination
            if (current_positions.count(next)) {
                EntityId other_id = current_positions.at(next);

                // Check if other ship is moving to our current position
                if (planned_moves.count(other_id)) {
                    Direction other_dir = planned_moves.at(other_id);
                    if (other_dir != Direction::STILL) {
                        std::shared_ptr<Ship> other_ship = me->ships.at(other_id);
                        Position other_next = game_map.normalize(other_ship->position.directional_offset(other_dir));

                        if (other_next == current) {
                            // Cycle detected (allowed)
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    void CollisionPolicy::resolve_conflict(
        const std::unordered_set<EntityId>& conflicting_ships,
        std::unordered_map<EntityId, Direction>& planned_moves,
        GameMap& game_map,
        const std::shared_ptr<Player>& me
    ) {
        if (conflicting_ships.empty()) return;

        // Strategy: force the ship with lowest priority to wait
        // Priority: ship with most halite has highest priority
        EntityId lowest_priority_ship = -1;
        Halite lowest_halite = 999999;

        for (auto it = conflicting_ships.begin(); it != conflicting_ships.end(); ++it) {
            EntityId ship_id = *it;
            std::shared_ptr<Ship> ship = me->ships.at(ship_id);
            if (ship->halite < lowest_halite) {
                lowest_halite = ship->halite;
                lowest_priority_ship = ship_id;
            }
        }

        // Force lowest priority ship to stay still
        if (lowest_priority_ship != -1) {
            planned_moves[lowest_priority_ship] = Direction::STILL;
        }
    }

    std::unordered_map<Position, std::vector<EntityId>> CollisionPolicy::build_destination_map(
        const std::unordered_map<EntityId, Direction>& planned_moves,
        GameMap& game_map,
        const std::shared_ptr<Player>& me
    ) const {
        std::unordered_map<Position, std::vector<EntityId>> dest_map;

        for (auto it = planned_moves.begin(); it != planned_moves.end(); ++it) {
            EntityId ship_id = it->first;
            Direction direction = it->second;

            std::shared_ptr<Ship> ship = me->ships.at(ship_id);
            Position destination = game_map.normalize(ship->position.directional_offset(direction));
            dest_map[destination].push_back(ship_id);
        }

        return dest_map;
    }

} // namespace hlt