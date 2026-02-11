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

	std::vector<Position> NavigationSystem::find_path(const Position& from, Position& to, bool avoid_ennemies = false) {
		/**
		* A*
		*/
		if (from == to) {
			return{ from };
		}
		// priority queue, sorted node by growinf f_cost
		std::priority_queue<PathNode, std::vector<PathNode>, PathNodeCompare> open_set;
		// all previously visited location
		std::set<Position> closed_set;
		// map of all unexplored node
		std::map<Position, PathNode> all_nodes;

		//Starting Node
		PathNode start_node(from, 0, calculate_heuristic(from, to), Position(-1, -1));
		open_set.push(start_node);
		all_nodes[from] = start_node;

		int max_iteration = map->width * map->height;
		int iteration = 0;

		while (!open_set.empty() && iteration < max_iteration) {
			iteration++;

			PathNode current = open_set.top();
			open_set.pop();

			// Ignore if already visited
			if (closed_set.find(current.position) != closed_set.end()) {
				continue;
			}

			closed_set.insert(current.position);
			
			// if arrived at destination
			if (current.position == to) {
				return reconstruct_path(all_nodes, from, to);
			}

			std::vector<Position> neighbours = get_neighbours(current.position);

			for (const Position& neighbour : neighbours) {
				if (closed_set.find(neighbour) != closed_set.end()) {
					continue;
				}

				if (avoid_ennemies && !is_position_safe(neighbour)) {
					continue;
				}

				int move_cost = calculate_move_cost(current.position, neighbour);
				int tentative_g_cost = current.g_cost + move_cost;

				// check if better path to neigbour already exist
				auto existing = all_nodes.find(neighbour);
				if (existing != all_nodes.end() && tentative_g_cost >= existing->second.g_cost) {
					continue;
				}

				int h_cost = calculate_heuristic(neighbour, to);
				PathNode neighbour_node(neighbour, tentative_g_cost, h_cost, current.position);

				open_set.push(neighbour_node);
				all_nodes[neighbour] = neighbour_node;
			}

		}

		return { from, to };
	}


}