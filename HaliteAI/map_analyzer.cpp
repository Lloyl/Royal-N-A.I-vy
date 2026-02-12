#include "map_analyzer.hpp"
#include "game.hpp"

#include <cmath>
#include <algorithm>

using namespace hlt;

namespace hlt {
	MapAnalyzer::MapAnalyzer(const GameMap* game_map) : map(game_map), 
		map_width(game_map->width), map_height(game_map->height),
		last_dropoff_update_turn(-1),
		last_cluster_update_turn(-1),
		last_enemy_update_turn(-1)
	{ }

	void MapAnalyzer::update_dropoffs(Game& game){
		dropoff_positions.clear();
		dropoff_positions.push_back(game.me->shipyard->position);

		for (const auto& dropoff_pair : game.me->dropoffs) {
			dropoff_positions.push_back(dropoff_pair.second->position);
		}

		last_dropoff_update_turn = game.turn_number;

		compute_distances_and_costs();

	}

	void MapAnalyzer::update_enemies(Game& game) {
		enemy_ship_positions.clear();

		for (const auto& player_pair : game.players) {
			PlayerId player_id = player_pair->id;

			if (player_id == game.my_id) {
				continue;
			}

			std::vector<Position> enemy_positions;
			for (const auto& ship_pair : player_pair->ships) {
				enemy_positions.push_back(ship_pair.second->position);
			}
			enemy_ship_positions[player_id] = enemy_positions;
		}

		last_enemy_update_turn = game.turn_number;
	}

	void MapAnalyzer::update_clusters(Game& game) {
		compute_halite_density();
		detect_clusters();
		last_cluster_update_turn = game.turn_number;
	}

	void MapAnalyzer::update_full(Game& game) {
		update_dropoffs(game);
		update_enemies(game);
		update_clusters(game);
	}

	void MapAnalyzer::update(Game& game, bool force_cluster, bool force_dropoff) {
		// enemies are always updated
		update_enemies(game);

		int current_dropoff_count = 1 + static_cast<int>(game.me->dropoffs.size());
		int stored_dropoff_count = static_cast<int>(dropoff_positions.size());
		bool new_dropoff_created = (current_dropoff_count > stored_dropoff_count);

		if (force_dropoff || new_dropoff_created || last_dropoff_update_turn == -1) {
			update_dropoffs(game);
		}

		bool should_update_clusters = force_cluster ||
			last_cluster_update_turn == -1 ||
			(game.turn_number - last_cluster_update_turn) >= 5;
		
		if (should_update_clusters) {
			update_clusters(game);
			last_cluster_update_turn = game.turn_number;
		}
	}
	
	Position MapAnalyzer::normalize_position(int x, int y) const {
		int norm_x = ((x % map_width) + map_width) % map_width;
		int norm_y = ((y % map_height) + map_height) % map_height;
		
		return Position(norm_x, norm_y);
	}

	void MapAnalyzer::compute_halite_density() {
		/**
		* For each map cell compute :
		*   halite_amout on the cell
		*   halite_density average Halite value in a 5x5 zone centered on the cell
		* O (width x height x 25)
		*/
		const int radius = 2;

		for (int y = 0; y < map_height; y++) {
			for (int x = 0; x < map_width; x++) {
				Position pos(x, y);

				int total_halite = 0;
				int cell_count = 0;

				for (int dy = -radius; dy <= radius; ++dy) {
					for (int dx = radius; dx <= radius; ++dx) {
						Position normalized_neighbour = normalize_position(x + dx, y + dy);
						total_halite += map->cells[normalized_neighbour.y][normalized_neighbour.x].halite;
						cell_count++;
					}
				}
				auto& data = cell_data[pos];
				data.halite_amount = map->cells[y][x].halite;
				data.halite_density = static_cast<double>(total_halite) / cell_count;
			}
		}
	}

	void MapAnalyzer::bfs_from_dropoff(const Position& dropoff) {
		/**
		* Start a breadth first search for each dropoff
		* Propagate distance and cost for each cell
		*/
		std::queue<Position> to_visit;
		std::map<Position, int>distances;
		std::map<Position, int>costs;

		to_visit.push(dropoff);
		distances[dropoff] = 0;
		costs[dropoff] = 0;
		
		while (!to_visit.empty()) {
			Position current = to_visit.front();
			to_visit.pop();

			int current_distance = distances[current];
			int current_cost = costs[current];

			std::vector<Position> neighbours = {
				normalize_position(current.x, current.y - 1),
				normalize_position(current.x, current.y + 1),
				normalize_position(current.x + 1, current.y),
				normalize_position(current.x - 1, current.y)
			};

			for (const Position& neighbour : neighbours) {
				if (distances.find(neighbour) == distances.end()) {
					int new_distance = current_distance + 1;
					int move_cost = static_cast<int>(
						std::round(map->cells[current.y][current.x].halite * 0.1)
						);
					int new_cost = current_cost + move_cost;
					
					distances[neighbour] = new_distance;
					costs[neighbour] = new_cost;
					
					to_visit.push(neighbour);

					auto& data = cell_data[neighbour];
					if (new_distance < data.distance_to_nearest_dropoff) {
						data.distance_to_nearest_dropoff = new_distance;
						data.cost_to_nearest_dropoff = new_cost;
						data.nearest_dropoff = dropoff;
					}
				}
			}
		}
	}

	void MapAnalyzer::detect_clusters() {
		/**
		* Divide the map into 8x8 bloc
		* for each bloc compute total halite_value and halite density
		* sort clusters by riches 
		* allow ship assigning to whole bloc rather than a cell
		*/ 
		clusters.clear();

		const int cluster_size = 3;

		for (int c_y = 0; c_y < map_height; c_y += cluster_size) {
			for (int c_x = 0; c_x < map_width; c_x += cluster_size) {
				HaliteCluster cluster;
				int total_halite = 0;
				int cell_count = 0;

				for (int y = c_y;y < std::min(c_y + cluster_size, map_height); ++y) {
					for (int x = c_x;x < std::min(c_x + cluster_size, map_width); ++x) {
						Position pos(x,y);
						int halite = map->cells[pos.y][pos.x].halite;
						total_halite += halite;
						cell_count++;
						cluster.cells.push_back(pos);
					}
				}

				cluster.center = Position(c_x + cluster_size / 2, c_y + cluster_size / 2);
				cluster.total_halite = total_halite;
				cluster.avg_density = static_cast<double>(total_halite) / cell_count;

				clusters.push_back(cluster);
			}
		}

		std::sort(clusters.begin(), clusters.end(), [](const HaliteCluster& a, const HaliteCluster& b) {
			return a.total_halite > b.total_halite;
			});
	}

	int MapAnalyzer::calculate_distance_const(const Position& source, const Position& target) const {
		Position normalized_source = normalize_position(source.x, source.y);
		Position normalized_target = normalize_position(target.x, target.y);

		int dx = std::abs(normalized_source.x - normalized_target.x);
		int dy = std::abs(normalized_source.y - normalized_target.y);

		int toroidal_dx = std::min(dx, map_width - dx);
		int toroidal_dy = std::min(dy, map_height - dy);

		return toroidal_dx + toroidal_dy;
	}
	// deprecated ??
	void MapAnalyzer::update_enemy_position(Game& game) {
		enemy_ship_positions.clear();

		for (const auto& player : game.players) {
			// disregard self
			if (player->id == game.my_id) {
				continue;
			}

			std::vector<Position> positions;

			for (const auto& ship_pair : player->ships) {
				positions.push_back(ship_pair.second->position);
			}

			enemy_ship_positions[player->id] = positions;
		}
	}

	void MapAnalyzer::compute_distances_and_costs() {
		for (auto& pair : cell_data) {
			pair.second.distance_to_nearest_dropoff = map_height * map_width;
			pair.second.cost_to_nearest_dropoff = 9999999;
		}
		for (const Position& dropoff : dropoff_positions) {
			bfs_from_dropoff(dropoff);
		}
	}

	double MapAnalyzer::get_halite_density(const Position& pos) const {
		auto it = cell_data.find(pos);
		return (it != cell_data.end()) ? it->second.halite_density : 0.0;
	}

	int MapAnalyzer::get_distance_to_dropoff(const Position& pos) const {
		// give distance to nearest dropoff
		auto it = cell_data.find(pos);
		return (it != cell_data.end()) ? it->second.distance_to_nearest_dropoff : 0;
	}

	int MapAnalyzer::get_cost_to_dropoff(const Position& pos) const {
		auto it = cell_data.find(pos);
		return (it != cell_data.end()) ? it->second.cost_to_nearest_dropoff : 0;
	}

	Position MapAnalyzer::get_nearest_dropoff(const Position& pos) const {
		auto it = cell_data.find(pos);
		return (it != cell_data.end()) ? it->second.nearest_dropoff : Position(0, 0);
	}

	std::vector<HaliteCluster> MapAnalyzer::get_rich_cluster(int min_total_halite) const {
		std::vector<HaliteCluster> rich_clusters;

		for (const auto& cluster : clusters) {
			if (cluster.total_halite >= min_total_halite) {
				rich_clusters.push_back(cluster);
			}
		}

		return rich_clusters;
	}

	bool MapAnalyzer::is_zone_contested(const Position& pos, int radius) const {
		for (const auto& enemy_entry : enemy_ship_positions) {
			for(const Position& enemy_pos : enemy_entry.second){
				int distance = calculate_distance_const(pos, enemy_pos);

				if (distance <= radius) {
					return true;
				}
			}
		}
		return false;
	}

	int MapAnalyzer::calculate_travel_cost(const Position& from, const Position& to) const {
		auto it = cell_data.find(from);
		if (it != cell_data.end() && it->second.nearest_dropoff == to) {
			return it->second.cost_to_nearest_dropoff;
		}
		
		int distance = calculate_distance_const(from, to);
		double avg_halite = (it != cell_data.end()) ? it->second.halite_density : 50.0;
		int estimated_cost_per_cell = static_cast<int>(avg_halite * 0.1);

		return distance * estimated_cost_per_cell;
	}

	std::vector<Position> MapAnalyzer::get_position_in_radius(const Position& center, int radius) const {
		std::vector<Position> positions;

		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				if (std::abs(dx) + std::abs(dy) <= radius) {
					Position pos = normalize_position(center.x + dx, center.y + dy);
					positions.push_back(pos);
				}
			}
		}
		return positions;
	}
}