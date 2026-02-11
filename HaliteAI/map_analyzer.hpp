#pragma once

#include "game_map.hpp"
#include "position.hpp"
#include "types.hpp"

#include <vector>
#include <map>
#include <queue>
#include <memory>

namespace hlt {

	struct Game; //forward declaration

	struct CellInfo {
		int halite_amount;
		double halite_density;
		int distance_to_nearest_dropoff;
		int cost_to_nearest_dropoff;
		Position nearest_dropoff;

		CellInfo() : halite_amount(0), halite_density(0.0),
			distance_to_nearest_dropoff(0), cost_to_nearest_dropoff(0), nearest_dropoff(0, 0) {
		}
	};

	struct HaliteCluster {
		Position center;
		int total_halite;
		double avg_density;
		std::vector<Position> cells;

		HaliteCluster() : center(0, 0), total_halite(0), avg_density(0.0) {}
	};

	struct MapAnalyzer {
		const GameMap* map;
		int map_width;
		int map_height;

		std::map<Position, CellInfo> cell_data;
		std::vector<HaliteCluster> clusters;
		std::vector<Position> dropoff_positions;
		std::map<PlayerId, std::vector<Position>>enemy_ship_positions;

		MapAnalyzer(const GameMap* game_map);

		void update(Game& game);

		double get_halite_density(const Position& pos) const;
		int get_distance_to_dropoff(const Position& pos) const;
		int get_cost_to_dropoff(const Position& pos) const;
		Position get_nearest_dropoff(const Position& pos) const;

		std::vector<HaliteCluster> get_rich_cluster(int min_total_halite = 2000) const;
		bool is_zone_contested(const Position& pos, int radius = 5) const;
		int calculate_travel_cost(const Position& from, const Position& to) const;
		std::vector<Position> get_position_in_radius(const Position& center, int radius) const;
		int calculate_distance_const(const Position& source, const Position& target) const;

	private:
		void compute_halite_density();
		void compute_distances_and_costs();
		void detect_clusters();
		void bfs_from_dropoff(const Position& dropoff); // bfs -> breadth-first-search
		void update_enemy_position(Game& game);
		Position normalize_position(int x, int y) const; //rewrite function since I cannot use the one in game_map
	};
}