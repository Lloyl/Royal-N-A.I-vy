#include "navigation_system.hpp"
#include "game.hpp"
#include "ship.hpp"

#include <algorithm>
#include <cmath>

namespace hlt {
	NavigationSystem::NavigationSystem(GameMap* game_map, MapAnalyzer* analyzer) : map(game_map), analyzer(analyzer){
	}

	void NavigationSystem::reset_turn() {
		reserved_cells.clear();
		planned_moves.clear();
	}

	void NavigationSystem::update_ship_position(const Game& game) {
		ship_positions.clear();

		for (const auto& ship_pair : game.me->ships) {
			ship_positions[ship_pair.first] = ship_pair.second->position;
		}
	}

	int NavigationSystem::calculate_heuristic(const Position& from, const Position& to) const {
		return analyzer->calculate_distance_const(from, to);
	}

	int NavigationSystem::calculate_move_cost(const Position& current, const Position& next) const {
		int base_cost = static_cast<int>(std::round(map->cells[current.y][current.x].halite * 0.1)
			);
		auto reservation = reserved_cells.find(next);

		if (reservation != reserved_cells.end()) {
			base_cost += 1000;// High penalty to avoid collisions
		}

		if (analyzer->is_zone_contested(next, 3)) {
			base_cost += 50;
		}

		return base_cost;
	}

	std::vector<Position> NavigationSystem::get_neighbours(const Position& pos) const {
		return {
			map->normalize(Position(pos.x, pos.y - 1)),
			map->normalize(Position(pos.x, pos.y + 1)),
			map->normalize(Position(pos.x - 1, pos.y)),
			map->normalize(Position(pos.x + 1, pos.y)),
		};
	}

	bool NavigationSystem::is_position_safe(const Position& pos)const {
		return !analyzer->is_zone_contested(pos, 2);
	}

	std::vector<Position> NavigationSystem::reconstruct_path(const std::map<Position, PathNode>& nodes, const Position& start, const Position& end) const {
		std::vector<Position> path;
		Position current = end;

		while (current != start) {
			path.push_back(current);

			auto it = nodes.find(current);
			if (it == nodes.end() || it->second.parent == Position(-1, -1)) {
				break;
			}
			current = it->second.parent;
		}

		path.push_back(start);

		std::reverse(path.begin(), path.end());

		return path;
	}
}