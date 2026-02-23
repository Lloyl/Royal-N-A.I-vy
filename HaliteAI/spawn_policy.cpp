#include "spawn_policy.hpp"
#include "constants.hpp"

namespace hlt {

    SpawnPolicy::SpawnPolicy()
        : min_turns_remaining_(200),
        min_map_halite_percentage_(0.33f) {
    }

    bool SpawnPolicy::should_spawn(
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        int turn_number,
        int initial_halite_on_map
    ) {
        // Check if we have enough halite to spawn
        if (me->halite < constants::SHIP_COST) {
            return false;
        }

        // Check if enough turns remaining
        int turns_remaining = constants::MAX_TURNS - turn_number;
        if (turns_remaining < min_turns_remaining_) {
            return false;
        }

        // Check if enough halite remains on map
        int remaining_halite = calculate_remaining_halite(game_map);
        float halite_percentage = static_cast<float>(remaining_halite) / static_cast<float>(initial_halite_on_map);

        if (halite_percentage < min_map_halite_percentage_) {
            return false;
        }

        // Check if shipyard is blocked
        if (is_shipyard_blocked(game_map, me)) {
            return false;
        }

        return true;
    }

    void SpawnPolicy::set_min_turns_remaining(int turns) {
        min_turns_remaining_ = turns;
    }

    void SpawnPolicy::set_min_map_halite_percentage(float percentage) {
        min_map_halite_percentage_ = percentage;
    }

    int SpawnPolicy::calculate_remaining_halite(GameMap& game_map) const {
        int total = 0;

        for (int y = 0; y < game_map.height; ++y) {
            for (int x = 0; x < game_map.width; ++x) {
                total += game_map.at(Position(x, y))->halite;
            }
        }

        return total;
    }

    bool SpawnPolicy::is_shipyard_blocked(GameMap& game_map, const std::shared_ptr<Player>& me) const {
        if (!me->shipyard) return true;

        const MapCell* cell = game_map.at(me->shipyard->position);
        return cell->is_occupied();
    }

} // namespace hlt