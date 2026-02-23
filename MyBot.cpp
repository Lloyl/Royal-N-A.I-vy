#include "game.hpp"
#include "constants.hpp"
#include "log.hpp"

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

    Game game;

    game.ready("Nelson");


    for (;;) {
        game.update_frame();
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;
		vector<Command> command_queue;

        if (!game.end_turn(command_queue)) {
            break;
        }
    }

    return 0;
}