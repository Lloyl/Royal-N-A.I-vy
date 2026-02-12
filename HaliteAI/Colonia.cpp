#include "game.hpp"
#include "constants.hpp"
#include "log.hpp"
#include "map_analyzer.hpp"
#include "navigation_system.hpp"

#include <random>
#include <ctime>

using namespace std;
using namespace hlt;

#ifdef _DEBUG
# define LOG(X) log::log(X);
#else
# define LOG(X)
#endif // DEBUG

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

    MapAnalyzer analyzer(game.game_map.get());
    NavigationSystem nav_system(game.game_map.get(), &analyzer);

    LOG("Map Analyzer and NavigationSystem initialized")

    game.ready("Colonia");

    LOG("Colonia succefffully created. ID :" + to_string(game.my_id));

    for (;;) {
        game.update_frame();
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        LOG("=== Turn" + to_string(game.turn_number) + " ===")

        analyzer.update(game);
        nav_system.reset_turn();
        nav_system.set_current_turn(game.my_id);
        nav_system.update_ship_position(game);

        LOG("Ships:" + to_string(me->ships.size()) + "| Halite: " + to_string(me->halite))

        vector<Command> command_queue;

        auto rich_clusters = analyzer.get_rich_cluster(1000);
        LOG("Rich clusters found:" + to_string(rich_clusters.size()))

        //Phase 1 decision making per ship
        int ship_index = 0;
        for (const auto& ship_iterator : me->ships) {
            shared_ptr<Ship> ship = ship_iterator.second;

            Position destination;
            int distance_to_dropoff = analyzer.get_distance_to_dropoff(ship->position);
            int turns_remaining = constants::MAX_TURNS - game.turn_number;

            if (ship->halite > 900 || distance_to_dropoff >= turns_remaining - 10) {
                destination = analyzer.get_nearest_dropoff(ship->position);
                LOG("Ship " + to_string(ship->id) + "Returning (cargo = " + to_string(ship->halite) + ")")
            }
            else {
                if (!rich_clusters.empty()) {
                    int cluster_index = ship_index % min(3, (int)rich_clusters.size());
                    destination = rich_clusters[cluster_index].center;
                    LOG("Ship " + to_string(ship->id) + "Collecting at cluster " + to_string(cluster_index)  )
                }
                else {
                    destination = ship->position;
                    LOG("Ship " + to_string(ship->id) + "IDLE")
                }
            }

            nav_system.add_ship_plan(ship, destination);
            ship_index++;
        }

        //phase 2 execute all orders
        LOG("Executing navigation plans...")

        auto moves = nav_system.execute_all_plans();

        //Phase 3 Create Commands
        for (const auto& ship_iterator : me->ships) {
            shared_ptr<Ship> ship = ship_iterator.second;

            auto move_it = moves.find(ship->id);
            if (move_it != moves.end()) {
                command_queue.push_back(ship->move(move_it->second));
            }
            else {
                command_queue.push_back(ship->stay_still());
            }
        }

        //Phase 4 spawn ships
        if (game.turn_number <= 250 && me->halite >= constants::SHIP_COST && !game_map->at(me->shipyard)->is_occupied()) {
            LOG("Spawing new ship")
            command_queue.push_back(me->shipyard->spawn());
        }

        if (!game.end_turn(command_queue)) {
            break;
        }

            
    }

    return 0;


}