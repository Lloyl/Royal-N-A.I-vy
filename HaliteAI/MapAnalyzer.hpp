#pragma once

#include "game_map.hpp"
#include "position.hpp"
#include "types.hpp"

#include <vector>
#include <map>
#include <queue>
#include <memory>

namespace hlt {
	struct CellInfo {
		int hallite_amount;
		double hallite_density;
		int distance_to_nearest_dropoff;
		int cost_to_nearest_dropoff;
		Position nearest_dropoff;

		CellInfo() : hallite_amount(0), hallite_density(0.0),
			distance_to_nearest_dropoff(0), cost_to_nearest_dropoff(0), nearest_dropoff(0, 0) {
		}
	};

	struct HalliteCluster {
		Position center;
		int total_hallite;
		double avg_density;
		std::vector<Position> cells;

		HalliteCluster() : center(0, 0), total_hallite(0), avg_density(0.0) {}
	};

	struct MapAnalyzer {
		const GameMap* map;
		int map_width;
		int map_height;

		std::map<Position, CellInfo> cell_data;
		std::vector<HalliteCluster> clusters;
		std::vector<Position> dropoff_position;

		MapAnalyzer(const GameMap& game_map);

		void update(const GameMap& game);

		double get_halite_density(const Position& pos) const;
		int get_distance_to_dropoff(const Position& pos) const;
		int get_cost_to_dropoff(const Position& pos) const;
		Position get_nearest_dropoff(const Position& pos) const;

		std::vector<HalliteCluster> get_rich_cluster(int min_total_halite = 2000) const;
		bool is_zone_contested(const Position& pos, int radius = 5) const;
		int calculate_travel_cost(const Position& from, const Position& to) const;
		std::vector<Position> get_position_in_radius(const Position& center, int radius) const;

	private:
		void compute_halite_density();
		void compute_distances_and_cost();
		void detect_clusters();
		void bfs_from_dropoff(); // bfs -> breadth-first-search
		int manhattan_distance(const Position& a, const Position& b) const;
		Position normalize_position(const Position& pos) const;
	};
}