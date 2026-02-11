#pragma once

#include "game_map.hpp"
#include "position.hpp"
#include "types.hpp"
#include "map_analyzer.hpp"

#include <vector>
#include <map>
#include <set>
#include <queue>
#include <memory>

namespace hlt {
	struct Game; //forward declaration
	struct Ship; //forward declaration

	struct PathNode {
		/**
		* Node for A* pathfinding
		*/
		Position position;
		int g_cost;			//departure cost
		int h_cost;			//heuristic cost
		int f_cost;			//g_cost + h_cost
		Position parent;

		PathNode() : position(0,0), g_cost(0), h_cost(0), f_cost(0), parent(-1, -1){}
		PathNode(Position pos, int g, int h, Position pos2) : position(pos), g_cost(g), h_cost(h),
			f_cost(g + h), parent(pos2) {}

	};

	struct PathNodeCompare {
		bool operator()(const PathNode& a, const PathNode& b)const {
			return a.f_cost > b.f_cost;
		}
	};

	struct CellReservation {
		EntityId ship_id;
		int priority;
		Position destination;

		CellReservation() : ship_id(-1), priority(0), destination(0,0){}
		CellReservation(EntityId id, int prio, Position dest) : ship_id(id), priority(prio), destination(dest) {}

	};

	struct ShipMovementPlan {
		std::shared_ptr<Ship> ship;
		Position destination;
		int priority;

		ShipMovementPlan() : ship(nullptr), destination(0,0), priority(0){}
		ShipMovementPlan(std::shared_ptr<Ship>s, Position dest, int prio) : ship(s), destination(dest), priority(prio) {}
	};

	struct NavigationSystem {
		GameMap* map;
		MapAnalyzer* analyzer;

		std::map<Position, CellReservation> reserved_cells;
		std::map<EntityId, Direction> planned_moves;
		std::map<EntityId, Position> ship_positions;
		std::vector<ShipMovementPlan> pending_plans;

		NavigationSystem(GameMap* game_map, MapAnalyzer* analyzer);
	

		void reset_turn();

		void update_ship_position(const Game& game);
		
		void add_ship_plan(const std::shared_ptr<Ship>& ship, const Position& destination, int priority = 0);

		std::map<EntityId, Direction> execute_all_plans(); //execute order 66

		std::vector<Position>find_path(const Position& from, const Position& to, bool avoid_ennemies = false);
		
		Direction navigate_ship(const std::shared_ptr<Ship>& ship, const Position& destination, int priority = 0);
		
		bool is_cell_available(const Position& pos, EntityId, int priority) const;

		bool reserve_cell(const Position& pos, EntityId ship_id, int priority, const Position& destination);

		Direction get_safe_move(const std::shared_ptr<Ship>& ship, const Position& destination, int priority);

		int calculate_ship_priority(const std::shared_ptr<Ship>& ship) const;

	private:
		
		int calculate_heuristic(const Position& from, const Position& to) const;
		int calculate_move_cost(const Position& current, const Position& next) const;
		std::vector<Position> reconstruct_path(const std::map<Position, PathNode>& nodes, const Position& start, const Position& end) const;
		std::vector<Position> get_neighbours(const Position& pos) const;
		bool is_position_safe(const Position& pos) const;
	};
}