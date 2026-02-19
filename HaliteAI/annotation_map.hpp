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
			is_inspired(false) {}
	};

}