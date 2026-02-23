#include "game.hpp"
#include "constants.hpp"
#include "log.hpp"
#include "HaliteAI/annotation_map.hpp"
#include "HaliteAI/planning_strategy.hpp"
#include "HaliteAI/spawn_policy.hpp"
#include "HaliteAI/dropoff_policy.hpp"
#include "HaliteAI/collision_policy.hpp"

#include <random>
#include <ctime>
#include <chrono>
#include <string>
#include <sstream>

using namespace std;
using namespace hlt;

#ifdef _DEBUG
# define LOG(X) log::log(X);
#else
# define LOG(X)
#endif

int main(int argc, char* argv[]) {
    unsigned int rng_seed;
    if (argc > 1) {
        rng_seed = static_cast<unsigned int>(stoul(argv[1]));
    }
    else {
        rng_seed = static_cast<unsigned int>(time(nullptr));
    }
    mt19937 rng(rng_seed);

    Game game;
    // Initialize strategy components
    PlanningStrategy planning_strategy;
    SpawnPolicy spawn_policy;
    DropoffPolicy dropoff_policy;
    CollisionPolicy collision_policy;

    // Calculate initial halite on map (for spawn policy)
    int initial_halite_on_map = 0;
    for (int y = 0; y < game.game_map->height; ++y) {
        for (int x = 0; x < game.game_map->width; ++x) {
            initial_halite_on_map += game.game_map->at(Position(x, y))->halite;
        }
    }

    log::log("Initial halite on map: " + to_string(initial_halite_on_map));
    log::log("Map size: " + to_string(game.game_map->width) + "x" + to_string(game.game_map->height));

    stringstream ss;
    ss << game.players.size();
    log::log("Number of players: " + ss.str());

    // Configure policies based on player count
    int num_players = static_cast<int>(game.players.size());

    // Adjust dropoff policy for map size
    int map_size = game.game_map->width;
    if (map_size <= 32) {
        dropoff_policy.set_min_distance(8);
        dropoff_policy.set_evaluation_radius(8);
    }
    else if (map_size <= 48) {
        dropoff_policy.set_min_distance(10);
        dropoff_policy.set_evaluation_radius(10);
    }
    else {
        dropoff_policy.set_min_distance(12);
        dropoff_policy.set_evaluation_radius(12);
    }

    log::log("Dropoff min distance: " + to_string(map_size <= 32 ? 8 : (map_size <= 48 ? 10 : 12)));

    game.ready("Nelson - ReCurse");

    log::log("==============================================");
    log::log("        Nelson - RECURSE BOT INITIALIZED");
    log::log("==============================================");


    for (;;) {
        auto turn_start = chrono::high_resolution_clock::now();

        game.update_frame();
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        log::log("");
        log::log("==============================================");
        stringstream turn_msg;
        turn_msg << "          TURN " << game.turn_number << " / " << constants::MAX_TURNS << "               ";
        log::log(turn_msg.str());
        log::log("==============================================");

        stringstream info_msg;
        info_msg << "Ships: " << me->ships.size()
            << " | Halite: " << me->halite
            << " | Dropoffs: " << me->dropoffs.size();
        log::log(info_msg.str());

        vector<Command> command_queue;

        // ===== STEP 1: BUILD ANNOTATION MAP =====
        log::log("");
        log::log("--- STEP 1: BUILDING ANNOTATION MAP ---");
        auto annotation_start = chrono::high_resolution_clock::now();

        AnnotationMap annotation_map(game_map->width, game_map->height);
        annotation_map.build(*game_map, game.players, me->id, game.turn_number);

        auto annotation_end = chrono::high_resolution_clock::now();
        auto annotation_time = chrono::duration_cast<chrono::milliseconds>(
            annotation_end - annotation_start
        ).count();

        log::log("Annotation map built in " + to_string(annotation_time) + "ms");

        // Log some annotation statistics
        int inspired_cells = 0;
        int enemy_accessible_cells = 0;
        for (int y = 0; y < game_map->height; ++y) {
            for (int x = 0; x < game_map->width; ++x) {
                const AnnotationMap::CellInfo& cell_info = annotation_map.at(Position(x, y));
                if (cell_info.is_inspired) inspired_cells++;
                if (cell_info.enemy_accessible_next_turn) enemy_accessible_cells++;
            }
        }
        stringstream stats_msg;
        stats_msg << "Inspired cells: " << inspired_cells
            << " | Enemy accessible: " << enemy_accessible_cells;
        log::log(stats_msg.str());

        // ===== STEP 2: PLANNING STRATEGY =====
        log::log("");
        log::log("--- STEP 2: PLANNING SHIP MOVEMENTS ---");
        auto planning_start = chrono::high_resolution_clock::now();

        unordered_map<EntityId, Direction> planned_moves = planning_strategy.plan_turn(
            *game_map,
            annotation_map,
            me,
            game.players,
            game.turn_number,
            num_players
        );

        auto planning_end = chrono::high_resolution_clock::now();
        auto planning_time = chrono::duration_cast<chrono::milliseconds>(
            planning_end - planning_start
        ).count();

        log::log("Planning completed in " + to_string(planning_time) + "ms");
        log::log("Planned moves for " + to_string(planned_moves.size()) + " ships");

        // Log ship states
        int returning_ships = 0;
        int mining_ships = 0;
        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            EntityId ship_id = it->first;
            PlanningStrategy::ShipState& state = planning_strategy.get_ship_state(ship_id);
            if (state.is_returning) {
                returning_ships++;
            }
            else {
                mining_ships++;
            }
        }
        stringstream state_msg;
        state_msg << "Ship states: " << returning_ships << " returning, " << mining_ships << " mining";
        log::log(state_msg.str());

        // ===== STEP 3: SPAWN POLICY =====
        log::log("");
        log::log("--- STEP 3: SPAWN DECISION ---");

        bool should_spawn = spawn_policy.should_spawn(
            *game_map,
            annotation_map,
            me,
            game.turn_number,
            initial_halite_on_map
        );

        if (should_spawn) {
            log::log("✓ Spawn conditions met");
        }
        else {
            string reason = "";
            if (me->halite < constants::SHIP_COST) {
                reason = "insufficient halite";
            }
            else if (constants::MAX_TURNS - game.turn_number < 200) {
                reason = "too late in game";
            }
            else {
                reason = "insufficient map resources";
            }
            log::log("✗ Cannot spawn: " + reason);
        }

        // ===== STEP 4: DROPOFF POLICY =====
        log::log("");
        log::log("--- STEP 4: DROPOFF EVALUATION ---");

        DropoffPolicy::DropoffResult dropoff_result = dropoff_policy.evaluate_dropoff_location(
            *game_map,
            annotation_map,
            me,
            planned_moves
        );

        bool should_build_dropoff = false;
        EntityId dropoff_builder_id = -1;

        if (dropoff_result.has_location) {
            Position dropoff_pos = dropoff_result.location;
            stringstream drop_msg;
            drop_msg << " Dropoff location identified: ("
                << dropoff_pos.x << "," << dropoff_pos.y << ")";
            log::log(drop_msg.str());

            // Check if we have enough halite
            if (me->halite >= constants::DROPOFF_COST) {
                dropoff_policy.set_planned_dropoff(dropoff_pos);

                // Find nearest ship
                dropoff_builder_id = dropoff_policy.find_nearest_ship_for_dropoff(
                    dropoff_pos, *game_map, me
                );

                if (dropoff_builder_id != -1) {
                    shared_ptr<Ship> builder_ship = me->ships.at(dropoff_builder_id);
                    int dist = game_map->calculate_distance(builder_ship->position, dropoff_pos);
                    stringstream builder_msg;
                    builder_msg << "Ship " << dropoff_builder_id
                        << " assigned to build dropoff (distance: " << dist << ")";
                    log::log(builder_msg.str());

                    // Check if ship is at location
                    if (builder_ship->position == dropoff_pos) {
                        should_build_dropoff = true;
                        log::log(" Building dropoff NOW!");
                    }
                }
            }
            else {
                stringstream cost_msg;
                cost_msg << " Not enough halite for dropoff (need "
                    << constants::DROPOFF_COST << ", have " << me->halite << ")";
                log::log(cost_msg.str());
            }
        }
        else {
            log::log(" No suitable dropoff location found");
        }

        // ===== STEP 5: COLLISION RESOLUTION =====
        log::log("");
        log::log("--- STEP 5: COLLISION RESOLUTION ---");
        auto collision_start = chrono::high_resolution_clock::now();

        unordered_map<EntityId, Direction> resolved_moves = collision_policy.resolve_collisions(
            planned_moves,
            *game_map,
            me,
            game.turn_number
        );

        auto collision_end = chrono::high_resolution_clock::now();
        auto collision_time = chrono::duration_cast<chrono::milliseconds>(
            collision_end - collision_start
        ).count();

        log::log("Collision resolution completed in " + to_string(collision_time) + "ms");

        // Count changes
        int changes = 0;
        for (auto it = planned_moves.begin(); it != planned_moves.end(); ++it) {
            EntityId ship_id = it->first;
            Direction original_dir = it->second;
            if (resolved_moves.count(ship_id) && resolved_moves[ship_id] != original_dir) {
                changes++;
            }
        }
        log::log("Moves adjusted: " + to_string(changes));

        // ===== STEP 6: GENERATE COMMANDS =====
        log::log("");
        log::log("--- STEP 6: COMMAND GENERATION ---");

        int cmd_still = 0, cmd_north = 0, cmd_south = 0, cmd_east = 0, cmd_west = 0;

        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            EntityId ship_id = it->first;
            shared_ptr<Ship> ship = it->second;

            // Check if this ship should build dropoff
            if (should_build_dropoff && ship_id == dropoff_builder_id) {
                command_queue.push_back(ship->make_dropoff());
                dropoff_policy.clear_planned_dropoff();
                log::log("Ship " + to_string(ship_id) + " building dropoff");
                continue;
            }

            // Get resolved move
            Direction move_dir = Direction::STILL;
            if (resolved_moves.count(ship_id)) {
                move_dir = resolved_moves[ship_id];
            }

            // Count directions
            switch (move_dir) {
            case Direction::STILL: cmd_still++; break;
            case Direction::NORTH: cmd_north++; break;
            case Direction::SOUTH: cmd_south++; break;
            case Direction::EAST: cmd_east++; break;
            case Direction::WEST: cmd_west++; break;
            }

            command_queue.push_back(ship->move(move_dir));
        }

        stringstream cmd_msg;
        cmd_msg << "Commands: STILL=" << cmd_still
            << " N=" << cmd_north
            << " S=" << cmd_south
            << " E=" << cmd_east
            << " W=" << cmd_west;
        log::log(cmd_msg.str());

        // ===== STEP 7: SPAWN EXECUTION =====
        if (should_spawn) {
            // Verify shipyard is not occupied
            bool shipyard_occupied = game_map->at(me->shipyard)->is_occupied();

            // Check if any ship is moving to shipyard
            bool shipyard_target = false;
            Position shipyard_pos = me->shipyard->position;
            for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
                EntityId ship_id = it->first;
                shared_ptr<Ship> ship = it->second;
                if (resolved_moves.count(ship_id)) {
                    Position target = game_map->normalize(
                        ship->position.directional_offset(resolved_moves[ship_id])
                    );
                    if (target == shipyard_pos) {
                        shipyard_target = true;
                        break;
                    }
                }
            }

            if (!shipyard_occupied && !shipyard_target) {
                command_queue.push_back(me->shipyard->spawn());
                log::log(" Spawning new ship");
            }
            else {
                log::log(" Shipyard blocked, cannot spawn");
            }
        }

        // ===== STATISTICS =====
        auto turn_end = chrono::high_resolution_clock::now();
        auto total_time = chrono::duration_cast<chrono::milliseconds>(
            turn_end - turn_start
        ).count();

        log::log("");
        log::log("==============================================");
        log::log("Turn timing: " + to_string(total_time) + "ms total");
        log::log("  - Annotation: " + to_string(annotation_time) + "ms");
        log::log("  - Planning: " + to_string(planning_time) + "ms");
        log::log("  - Collision: " + to_string(collision_time) + "ms");
        log::log("==============================================");

        if (!game.end_turn(command_queue)) {
            break;
        }
    }

    return 0;
}