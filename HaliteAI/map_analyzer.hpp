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

	/**
	* @brief Store cell data for computation
	*/
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
	/**
	* @brief Halite Cluster infos
	*/
	struct HaliteCluster {
		Position center;
		int total_halite;
		double avg_density;
		std::vector<Position> cells;

		HaliteCluster() : center(0, 0), total_halite(0), avg_density(0.0) {}
	};
	/**
	* @brief System for map analysis
	*/
	struct MapAnalyzer {
		const GameMap* map;
		int map_width;
		int map_height;
		
		int last_dropoff_update_turn;
		int last_cluster_update_turn;
		int last_enemy_update_turn;

		std::map<Position, CellInfo> cell_data;
		std::vector<HaliteCluster> clusters;
		std::vector<Position> dropoff_positions;
		std::map<PlayerId, std::vector<Position>>enemy_ship_positions;
		/**
		* @brief Constructor
		*/
		MapAnalyzer(const GameMap* game_map);
		/**
		* @brief Full update (everything)
		*/
		void update_full(Game& game);
		/**
		* @brief Update dropoff location on the map, needs to be call when creating a new dropoff
		*/
		void update_dropoffs(Game& game);
		/**
		* @brief Update enemies position
		*/
		void update_enemies(Game& game);
		/**
		* @brief Update halite density and find new clusters
		*/
		void update_clusters(Game& game);
		/**
		* @brief Adaptative update
		* @param force_cluster If true force cluster update
		* @param force_dropoff If true force dropoff update
		*/
		void update(Game& game, bool force_clusters = false, bool force_dropoff = false);
		/**
		* @brief Get halite density of a given position
		*/
		double get_halite_density(const Position& pos) const;
		/**
		* @brief Get distance to the nearest dropoff
		*/
		int get_distance_to_dropoff(const Position& pos) const;
		/**
		* @brief Get cost to go to nearest dropoff
		*/
		int get_cost_to_dropoff(const Position& pos) const;
		/**
		* @brief Get position of the nearest dropoff
		*/
		Position get_nearest_dropoff(const Position& pos) const;
		/**
		* @brief Get all positions in a given radius
		*/
		std::vector<HaliteCluster> get_rich_cluster(int min_total_halite = 2000) const;
		/**
		* @brief Check if zone is contested by ennemies
		*/
		bool is_zone_contested(const Position& pos, int radius = 5) const;
		/**
		* @brief Compute cost between two positions
		*/
		int calculate_travel_cost(const Position& from, const Position& to) const;
		/**
		* @brief Compute cost to travel between two positions
		*/
		std::vector<Position> get_position_in_radius(const Position& center, int radius) const;
		/**
		* @brief Compute Manhattan geometry (const version)
		*/
		int calculate_distance_const(const Position& source, const Position& target) const;

	private:
		/**
		* @brief Compute halite density for each cell
		*/
		void compute_halite_density();
		/**
		* @brief Compute distance and cost to each dropoffs
		*/
		void compute_distances_and_costs();
		/**
		* @brief Detect halite clusters
		*/
		void detect_clusters();
		/**
		* @brief Breadth first search from a dropoff to estimate cost and distance
		*/
		void bfs_from_dropoff(const Position& dropoff); // bfs -> breadth-first-search
		/**
		* @brief Update enemy positon
		*/
		void update_enemy_position(Game& game);
		/**
		* @brief Normalize position on the torus
		*/
		Position normalize_position(int x, int y) const; //rewrite function since I cannot use the one in game_map
	};
}