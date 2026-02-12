#include "navigation_system.hpp"
#include "game.hpp"
#include "ship.hpp"

#include <algorithm>
#include <cmath>

namespace hlt {
	NavigationSystem::NavigationSystem(GameMap* game_map, MapAnalyzer* analyzer, int cache_validity_turn, int current_turn) : map(game_map), analyzer(analyzer), cache_validity_turns(5), current_turn(-1){
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

	std::vector<Position> NavigationSystem::find_path(const Position& from, const Position& to, bool avoid_ennemies) {
		/**
		* A*
		*/
		if (from == to) {
			return{ from };
		}

		PathCacheKey key{ from, to };
		auto cached = path_cache.find(key);

		if (cached != path_cache.end()) {
			int turn_since_calcualtion = current_turn - cached->second.turn_calculated;

			if (turn_since_calcualtion < cache_validity_turns) {
				return cached->second.path;
			}
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
		std::vector<Position> path = { from, to };
		path_cache[key] = CachedPath{ path, current_turn, 0 };

		return path;
	}

	bool NavigationSystem::is_cell_available(const Position& pos, EntityId ship_id, int priority) const {

		auto ship_pos = ship_positions.find(ship_id);
		if (ship_pos != ship_positions.end() && ship_pos->second == pos) {
			return true;
		}

		auto reservation = reserved_cells.find(pos);
		if (reservation == reserved_cells.end()) {
			return true;
		}

		// if space reserved by the ship
		if (reservation->second.ship_id == ship_id) {
			return true;
		}

		return priority > reservation->second.priority;
	}

	bool NavigationSystem::reserve_cell(const Position& pos, EntityId ship_id, int priority, const Position& destination) {
		if (!is_cell_available(pos, ship_id, priority)) {
			return false;
		}

		reserved_cells[pos] = CellReservation(ship_id, priority, destination);
		return true;
	}

	int NavigationSystem::calculate_ship_priority(const std::shared_ptr<Ship>& ship) const {
		int priority = 0;

		priority += ship->halite / 10;

		int distance_to_dropoff = analyzer->get_distance_to_dropoff(ship->position);
		if (distance_to_dropoff < 5) {
			priority += 50;
		}

		if (ship->halite > 900) {
			priority += 100;
		}

		return priority;
	}

	Direction NavigationSystem::get_safe_move(const std::shared_ptr<Ship>& ship, const Position& destination, int priority) {
		Position current = ship->position;

		if (current == destination) {
			reserve_cell(current, ship->id, priority, destination);
			return Direction::STILL;
		}

		std::vector<Direction> possible_directions = map->get_unsafe_moves(current, destination);
		possible_directions.push_back(Direction::STILL);

		struct MoveOption {
			Direction dir;
			Position target;
			int cost;
			bool available;
		};

		std::vector<MoveOption> options;

		for (Direction dir : possible_directions) {
			Position target = current.directional_offset(dir);
			target = map->normalize(target);

			MoveOption option;
			option.dir = dir;
			option.target = target;
			option.available = is_cell_available(target, ship->id, priority);

			if (dir == Direction::STILL) {
				// Moving option preferred
				option.cost = 100;
			}
			else {
				option.cost = calculate_move_cost(current, target);
			}

			options.push_back(option);
		}

		std::sort(options.begin(), options.end(), [](const MoveOption& a, const MoveOption& b) {
			if (a.available > b.available) {
				return a.available > b.available; //available first
			}
			return a.cost < b.cost; // then cost
			});
		
		// use best option
		for (const auto& option : options) {
			if (option.available) {
				if (reserve_cell(option.target, ship->id, priority, destination)) {
					planned_moves[ship->id] = option.dir;
					return option.dir;
				}
			}
		}

		// if no option available stay still
		reserve_cell(current, ship->id, priority, destination);
		planned_moves[ship->id] = Direction::STILL;
		return Direction::STILL;
	}
	void NavigationSystem::add_ship_plan(const std::shared_ptr<Ship>& ship, const Position& destination, int priority) {
		if (priority == 0) {
			priority = calculate_ship_priority(ship);
		}

		pending_plans.push_back(ShipMovementPlan(ship, destination, priority));
	}

	std::map<EntityId, Direction> NavigationSystem::execute_all_plans() {
		std::sort(pending_plans.begin(), pending_plans.end(), [](const ShipMovementPlan& a, const ShipMovementPlan& b) {
			return a.priority > b.priority;
			});

		for (const auto& plan : pending_plans) {
			navigate_ship(plan.ship, plan.destination, plan.priority);
		}

		pending_plans.clear();

		return planned_moves;
	}

	Direction NavigationSystem::navigate_ship(const std::shared_ptr<Ship>& ship,const Position& destination, int priority) {
		if (priority == 0) {
			priority = calculate_ship_priority(ship);
		}

		auto planned = planned_moves.find(ship->id);
		if (planned != planned_moves.end()) {
			return planned->second;
		}

		//if not enough Halite to move
		int cell_halite = map->cells[ship->position.y][ship->position.x].halite;
		if (ship->halite < cell_halite / 10) {
			reserve_cell(ship->position, ship->id, priority, destination);
			planned_moves[ship->id] = Direction::STILL;
			return Direction::STILL;
		}

		bool avoid_ennemies = (ship->halite >= 500); //avoid ennemy if at least half full
		std::vector<Position> path = find_path(ship->position, destination, avoid_ennemies);

		if (path.size() > 1) {
			Position next_pos = path[1];

			int dx = next_pos.x - ship->position.x;
			int dy = next_pos.y - ship->position.y;

			//wrap torrus
			if (std::abs(dx) > 1)dx = -dx / std::abs(dx);
			if (std::abs(dy) > 1)dy = -dy / std::abs(dy);

			Direction dir = Direction::STILL;

			if (dx == 1) dir = Direction::EAST;
			if (dx == -1) dir = Direction::WEST;
			if (dy == 1) dir = Direction::SOUTH;
			if (dy == -1) dir = Direction::NORTH;

			if (is_cell_available(next_pos, ship->id, priority)) {
				if (reserve_cell(next_pos, ship->id, priority, destination)) {
					planned_moves[ship->id] = dir;
					return dir;
				}
			}
		}

		return get_safe_move(ship, destination, priority);
	}
};
