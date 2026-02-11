#include "map_analyzer.hpp"
#include "game.hpp"

#include <cmath>
#include <algorithm>

using namespace hlt;

namespace hlt {
	MapAnalyzer::MapAnalyzer(const GameMap* game_map) : map(game_map), 
		map_width(game_map->width), map_height(game_map->height){ }

	void MapAnalyzer::update(Game& game){
		/**Update map info when called
		* Collect dropoff position 
		* Collect Enemy boat position
		* Compute Halite density
		* Compute distance and cost when moving to dropoff
		* Compute Hih density halite cluster
		*/
		dropoff_positions.clear();
		dropoff_positions.push_back(game.me->shipyard->position);

		for (const auto& dropoff_pair : game.me->dropoffs) {
			dropoff_positions.push_back(dropoff_pair.second->position);
		}

		update_enemy_position(game);
		compute_distances_and_costs();
		compute_halite_density();
		detect_clusters();

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
		cell_data.clear();

		for (int y = 0; y < map_height; y++) {
			for (int x = 0; x < map_width; x++) {
				Position pos(x, y);
				CellInfo info;
				info.halite_amount = map->cells[x][y].halite;

				int total_halite = 0;
				int cell_count = 0;

				for (int dy = -2; dy <= 2; ++dy) {
					for (int dx = 2; dx <= 2; ++dx) {
						Position normalized_neighbour = normalize_position(x + dx, y + dy);
						total_halite += map->cells[normalized_neighbour.y][normalized_neighbour.x].halite;
						cell_count++;
					}
				}

				info.halite_density = static_cast<double>(total_halite) / cell_count;
				cell_data[pos] = info;
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

			int current_dist = distances[current];
			int current_cost = costs[current];

			std::vector<Position> neighbours = {
				normalize_position(current.x, current.y - 1),
				normalize_position(current.x, current.y + 1),
				normalize_position(current.x + 1, current.y),
				normalize_position(current.x - 1, current.y)
			};

			for (const Position& neighbour : neighbours) {
				if (distances.find(neighbour) == distances.end()) {
					int move_cost = static_cast<int>(
						std::round(map->cells[current.y][current.x].halite * 0.1)
						);
					distances[neighbour] = current_dist + 1;
					costs[neighbour] = current_cost + move_cost;
					to_visit.push(neighbour);
				}
			}
		}
		
		for (const auto& entry : distances) {
			Position pos = entry.first;
			int dist = entry.second;
			int cost = costs[pos];

			if (cell_data.find(pos) == cell_data.end()) {
				cell_data[pos] = CellInfo();
			}

			if (cell_data[pos].distance_to_nearest_dropoff == 0 ||
				dist < cell_data[pos].distance_to_nearest_dropoff) {
				cell_data[pos].distance_to_nearest_dropoff = dist;
				cell_data[pos].cost_to_nearest_dropoff = cost;
				cell_data[pos].nearest_dropoff = dropoff;
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

		const int cluster_size = 8;

		for (int cy = 0; cy < map_height; cy += cluster_size) {
			for (int cx = 0; cx < map_width; cx += cluster_size) {
				HaliteCluster cluster;
				int total_halite = 0;
				int cell_count = 0;

				for (int dy = 0;dy < cluster_size && cy + dy < map_height; ++dy) {
					for (int dx = 0; dx < cluster_size && cx + dx < map_width; ++dx) {
						Position pos(cx + dx, cy + dy);
						int halite = map->cells[pos.y][pos.x].halite;
						total_halite += halite;
						cell_count++;
						cluster.cells.push_back(pos);
					}
				}

				if (total_halite > 0) {
					cluster.center = Position(cx + cluster_size / 2, cy + cluster_size / 2);
					cluster.total_halite = total_halite;
					cluster.avg_density = static_cast<double> (total_halite) / cell_count;

					clusters.push_back(cluster);
				}
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
		/**
		* Start a breadth first search for each dropoff
		* Compute distance to nearest dropoff from pos
		* cost to nearest dropoff from pos
		* nearest dropoff from pos
		*/
		for (const Position& dropoff : dropoff_positions) {
			bfs_from_dropoff(dropoff);
		}
	}

	double MapAnalyzer::get_halite_density(const Position& pos) const {
		auto it = cell_data.find(pos);
		return (it != cell_data.end()) ? it->second.halite_density : 0.0;
	}

	int MapAnalyzer::get_distance_to_dropoff(const Position& pos) const {
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
		int enemy_count = 0;

		for (const auto& enemy_entry : enemy_ship_positions) {
			for(const Position& enemy_pos : enemy_entry.second){
				int distance = calculate_distance_const(pos, enemy_pos);

				if (distance <= radius) {
					enemy_count++;
				}
			}
		}
		return enemy_count > 0;
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