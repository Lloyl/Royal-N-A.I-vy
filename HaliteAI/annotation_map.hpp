#pragma once

#include "game_map.hpp"
#include "game.hpp"
#include "position.hpp"
#include "ship.hpp"
#include <vector>
#include <map>
#include <set>

namespace hlt {
	/**
	* @brief Informations pre-calculated for a cell
	*/

	struct CellAnnotation {
		/// Distances
		int distance_to_ally_dropoff;
		int distance_to_nearest_ally;
		int distance_to_nearest_enemy;

		///Enemies
		bool has_enemy_ship;
		bool enemy_can_reach_next_turn;

		/// Dominance (allied_ship - enemy_ship) in a radius (3,5,10)
		int dominance;

		/// Attraction field value
		double attraction;

		/// Path marking
		bool will_be_mined;
		uint64_t occupied_turns;		// Bitfield

		/// Inspiration
		bool is_inspired;

		CellAnnotation() :
			distance_to_ally_dropoff(999),
			distance_to_nearest_ally(999),
			distance_to_nearest_enemy(999),
			has_enemy_ship(false),
			enemy_can_reach_next_turn(false),
			dominance(0),
			attraction(0.0),
			will_be_mined(false),
			occupied_turns(0),
			is_inspired(false) {
		}
	};

	struct AnnotationMap {
	public:
		AnnotationMap(GameMap* game_map);

		void update(const Game& game);

		const CellAnnotation& at(const Position& pos) const;
		CellAnnotation& at(const Position& pos);

		int get_dropoff_distance(const Position& pos) const;
		bool has_enemy_at(const Position& pos)const;
		int get_dominance(const Position& pos)const;
		double get_attraction(const Position& pos)const;
		bool is_cell_inspired(const Position& pos)const;

		/**
		* @brief Mark a path in annotation map
		*/
		void mark_path(const std::vector<Position>& path, bool will_mine_at_end = false);

		/**
		* @bried Check if a cell is occupy at a given time
		*/
		bool is_occupied_at_turn(const Position& pos, int turn_offset)const;

		/**
		* @brief Compute forbidden moves for a ship
		*/
		std::set<Direction> get_forbidden_moves(const std::shared_ptr<Ship>& ship, const Game& game)const;

	private:
		GameMap* map_;
		std::vector<std::vector<CellAnnotation>> annotations_;

		void calculate_distances(const Game& game);
		void mark_enemy_positions(const Game& game);
		void calculate_dominance(const Game& game, int radius = 5);
		void calculate_attraction_field(const Game& game);
		void calculate_inspiration(const Game& game);
		
		int manhattan_distance(const Position& a, const Position& b)const;
		std::vector<Position> get_neighbours(const Position& pos)const;

		/// Helpers for attraction field
		double apply_exponential_blur(int x, int y, double decay = 0.75)const;
	};
}