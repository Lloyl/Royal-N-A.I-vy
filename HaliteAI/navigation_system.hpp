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
	/**
	* @brief Node for A*
	*/
	struct PathNode {
		/**
		*@brief Node for A* pathfinding
		*/
		Position position;
		int g_cost;			//departure cost
		int h_cost;			//heuristic cost
		int f_cost;			//g_cost + h_cost
		Position parent;

		PathNode() : position(0, 0), g_cost(0), h_cost(0), f_cost(0), parent(-1, -1) {}
		PathNode(Position pos, int g, int h, Position pos2) : position(pos), g_cost(g), h_cost(h),
			f_cost(g + h), parent(pos2) {
		}

	};
	/**
	* @brief Comparator for priority queue 
	*/
	struct PathNodeCompare {
		bool operator()(const PathNode& a, const PathNode& b)const {
			return a.f_cost > b.f_cost;
		}
	};
	/**
	* @brief Info on cell reservation
	*/
	struct CellReservation {
		EntityId ship_id;
		int priority;
		Position destination;

		CellReservation() : ship_id(-1), priority(0), destination(0, 0) {}
		CellReservation(EntityId id, int prio, Position dest) : ship_id(id), priority(prio), destination(dest) {}

	};
	/**
	* @brief Movement plan for a ship
	*/
	struct ShipMovementPlan {
		std::shared_ptr<Ship> ship;
		Position destination;
		int priority;

		ShipMovementPlan() : ship(nullptr), destination(0, 0), priority(0) {}
		ShipMovementPlan(std::shared_ptr<Ship>s, Position dest, int prio) : ship(s), destination(dest), priority(prio) {}
	};
	/**
	* @brief Key for path cache
	*/
	struct PathCacheKey {
		Position from;
		Position to;

		bool operator<(const PathCacheKey& other)const{
			if (from.x != other.from.x) return from.x < other.from.x;
			if (from.y != other.from.y) return from.y < other.from.y;
			if (to.x != other.to.x) return to.x < other.to.x;
			return to.y < other.to.y;
		}
	};
	/**
	* @brief Chached path
	*/
	struct CachedPath {
		std::vector<Position> path;
		int turn_calculated;
		
		CachedPath() : path(), turn_calculated(0){}
		CachedPath(std::vector<Position>p, int turn) : path(p), turn_calculated(turn){}
	};
	/**
	*@brief Navigation System with A* and cell reservation
	*/
	struct NavigationSystem {
		GameMap* map;
		MapAnalyzer* analyzer;

		std::map<Position, CellReservation> reserved_cells;
		std::map<EntityId, Direction> planned_moves;
		std::map<EntityId, Position> ship_positions;
		std::vector<ShipMovementPlan> pending_plans;
		std::map<PathCacheKey, CachedPath> path_cache;

		int cache_validity_turns;
		int current_turn;
		int max_pathfinding_distance;

		/**
		* @brief Constructor
		*/
		NavigationSystem(GameMap* game_map, MapAnalyzer* analyzer, int cache_validity_turns, int current_turn);
		/**
		* @brief reset reservation at the start of each turn
		*/
		void reset_turn();
		/**
		* @brief Update all ship position for the current turn
		*/
		void update_ship_position(const Game& game);
		/**
		* @brief Add a movement plan to a ship
		* @param ship Ship to move
		* @param destination Final destination
		* @param priority Ship priority (if priority == 0, priority is computed)
		*/
		void add_ship_plan(const std::shared_ptr<Ship>& ship, const Position& destination, int priority = 0);
		/**
		* @brief Update the current turn number
		*/
		void set_current_turn(int turn) { current_turn = turn; }
		/**
		* @brief Clear old cache path values 
		*/
		void clear_old_cache();
		/**
		* @brief Execute all movement plan in priority order
		* @return Map ship_id -> Direction
		*/
		std::map<EntityId, Direction> execute_all_plans(); //execute order 66
		/**
		* @brief find optimal path using A*
		* @param from Starting position
		* @param to Ending position
		* @param avoid_enemies Avoid contested zones
		* @return List of position (start with from, ends with to)
		*/
		std::vector<Position>find_path(const Position& from, const Position& to, bool avoid_ennemies = false);
		/**
		* @brief Sail ship to destination
		* @param ship Ship to move
		* @param destination Final destination
		* @param priority Priority of the ship
		* @return Direction to move for the turn
		*/
		Direction navigate_ship(const std::shared_ptr<Ship>& ship, const Position& destination, int priority = 0);
		/**
		* @brief Check if a cell is available (non reserved or reservable)
		* @param pos Postion to check
		* @param ship_id ID of the ship attempting to reserve
		* @param priority Priority
		* @return true if the cell is usable
		*/
		bool is_cell_available(const Position& pos, EntityId, int priority) const;
		/**
		* @brief Reserve cell for a ship
		* @param pos Postion to reserve
		* @param ship_id ID of the ship reserving
		* @param priority Priority
		* @param destination Final ship destination
		* @return true if the cell is usable
		*/
		bool reserve_cell(const Position& pos, EntityId ship_id, int priority, const Position& destination);
		/**
		* @brief Find the best direction to a destination
		* @param ship Moving ship
		* @param destination Destination
		* @param priority Priority
		* @return Optimal direction (STILL if stuck)
		*/
		Direction get_safe_move(const std::shared_ptr<Ship>& ship, const Position& destination, int priority);
		/**
		* @brief Compute ship priority
		* @param ship Ship
		* @return Priority (depends on cargo, distance to dropoff and bonuses)
		*/
		int calculate_ship_priority(const std::shared_ptr<Ship>& ship) const;

	private:
		/**
		* @brief Compute heuristic for A* (Manhattan geometry)
		*/
		int calculate_heuristic(const Position& from, const Position& to) const;
		/**
		* @brief Compute cost to move toward a certain cell
		* @param current Current position
		* @param next Next Position
		* @return Halite cost
		*/
		int calculate_move_cost(const Position& current, const Position& next) const;
		/**
		* @brief Reconstruct a path from A* node
		*/
		std::vector<Position> reconstruct_path(const std::map<Position, PathNode>& nodes, const Position& start, const Position& end) const;
		/**
		* @brief Get neighbouring cell available for navigation
		*/
		std::vector<Position> get_neighbours(const Position& pos) const;
		/**
		* @brief Check if a position is safe (no close enemy)
		*/
		bool is_position_safe(const Position& pos) const;
	};
}