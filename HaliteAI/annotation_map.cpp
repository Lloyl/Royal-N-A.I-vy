#include "annotation_map.hpp"
#include "constants.hpp"
#include <queue>
#include <cmath>
#include <algorithm>

namespace hlt {

    AnnotationMap::AnnotationMap(GameMap* game_map)
        : map_(game_map)
    {
        annotations_.resize(map_->height);
        for (int y = 0; y < map_->height; ++y) {
            annotations_[y].resize(map_->width);
        }
    }

    void AnnotationMap::update(const Game& game) {
        // Reinitialize (execpt lasting path marking)
        for (int y = 0; y < map_->height; ++y) {
            for (int x = 0; x < map_->width; ++x) {
                auto& cell = annotations_[y][x];
                cell.distance_to_ally_dropoff = 999;
                cell.distance_to_nearest_ally = 999;
                cell.distance_to_nearest_enemy = 999;
                cell.has_enemy_ship = false;
                cell.enemy_can_reach_next_turn = false;
                cell.dominance = 0;
                cell.attraction = 0.0;
                cell.is_inspired = false;
            }
        }

        calculate_distances(game);
        mark_enemy_positions(game);
        calculate_dominance(game);
        calculate_attraction_field(game);
        calculate_inspiration(game);
        mark_enemy_reach(game);
    }

    const CellAnnotation& AnnotationMap::at(const Position& pos) const {
        return annotations_[pos.y][pos.x];
    }

    CellAnnotation& AnnotationMap::at(const Position& pos) {
        return annotations_[pos.y][pos.x];
    }

    int AnnotationMap::get_dropoff_distance(const Position& pos) const {
        return annotations_[pos.y][pos.x].distance_to_ally_dropoff;
    }

    bool AnnotationMap::has_enemy_at(const Position& pos) const {
        return annotations_[pos.y][pos.x].has_enemy_ship;
    }

    int AnnotationMap::get_dominance(const Position& pos) const {
        return annotations_[pos.y][pos.x].dominance;
    }

    double AnnotationMap::get_attraction(const Position& pos) const {
        return annotations_[pos.y][pos.x].attraction;
    }

    bool AnnotationMap::is_cell_inspired(const Position& pos) const {
        return annotations_[pos.y][pos.x].is_inspired;
    }

    int AnnotationMap::manhattan_distance(const Position& a, const Position& b) const {
        return map_->calculate_distance(a, b);
    }

    std::vector<Position> AnnotationMap::get_neighbors(const Position& pos) const {
        return {
            map_->normalize(Position(pos.x, pos.y - 1)),
            map_->normalize(Position(pos.x, pos.y + 1)),
            map_->normalize(Position(pos.x + 1, pos.y)),
            map_->normalize(Position(pos.x - 1, pos.y))
        };
    }

    void AnnotationMap::calculate_distances(const Game& game) {
        /// Dropoff positions
        std::vector<Position> dropoff_positions;
        dropoff_positions.push_back(game.me->shipyard->position);
        for (const auto& dropoff_pair : game.me->dropoffs) {
            dropoff_positions.push_back(dropoff_pair.second->position);
        }

        /// Allied ship positions
        std::vector<Position> ally_positions;
        for (const auto& ship_pair : game.me->ships) {
            ally_positions.push_back(ship_pair.second->position);
        }

        /// Enemy ship positions
        std::vector<Position> enemy_positions;
        for (const auto& player : game.players) {
            if (player->id == game.me->id) continue;
            for (const auto& ship_pair : player->ships) {
                enemy_positions.push_back(ship_pair.second->position);
            }
        }

        /// Compute distances
        for (int y = 0; y < map_->height; ++y) {
            for (int x = 0; x < map_->width; ++x) {
                Position pos(x, y);

                // Distance to closest dropoff
                int min_dropoff_dist = 999;
                for (const Position& dropoff : dropoff_positions) {
                    min_dropoff_dist = std::min(min_dropoff_dist, manhattan_distance(pos, dropoff));
                }
                annotations_[y][x].distance_to_ally_dropoff = min_dropoff_dist;

                /// Distance to closest ally
                int min_ally_dist = 999;
                for (const Position& ally : ally_positions) {
                    if (ally != pos) {
                        min_ally_dist = std::min(min_ally_dist, manhattan_distance(pos, ally));
                    }
                }
                annotations_[y][x].distance_to_nearest_ally = min_ally_dist;

                /// Distance to closest enemy
                int min_enemy_dist = 999;
                for (const Position& enemy : enemy_positions) {
                    min_enemy_dist = std::min(min_enemy_dist, manhattan_distance(pos, enemy));
                }
                annotations_[y][x].distance_to_nearest_enemy = min_enemy_dist;
            }
        }
    }

    void AnnotationMap::mark_enemy_positions(const Game& game) {
        for (const auto& player : game.players) {
            if (player->id == game.me->id) continue;

            for (const auto& ship_pair : player->ships) {
                Position enemy_pos = ship_pair.second->position;
                annotations_[enemy_pos.y][enemy_pos.x].has_enemy_ship = true;
            }
        }
    }

    void AnnotationMap::calculate_dominance(const Game& game, int radius) {
        for (int y = 0; y < map_->height; ++y) {
            for (int x = 0; x < map_->width; ++x) {
                Position center(x, y);

                int ally_count = 0;
                int enemy_count = 0;

                /// Count allies & ennemies within radius
                for (const auto& ship_pair : game.me->ships) {
                    if (manhattan_distance(center, ship_pair.second->position) <= radius) {
                        ally_count++;
                    }
                }

                for (const auto& player : game.players) {
                    if (player->id == game.me->id) continue;

                    for (const auto& ship_pair : player->ships) {
                        if (manhattan_distance(center, ship_pair.second->position) <= radius) {
                            enemy_count++;
                        }
                    }
                }

                annotations_[y][x].dominance = ally_count - enemy_count;
            }
        }
    }

    double AnnotationMap::apply_exponential_blur(int x, int y, double decay) const {
        double sum = 0.0;
        double total_weight = 0.0;

        int blur_radius = 3;

        for (int dy = -blur_radius; dy <= blur_radius; ++dy) {
            for (int dx = -blur_radius; dx <= blur_radius; ++dx) {
                Position neighbor = map_->normalize(Position(x + dx, y + dy));
                int halite = map_->at(neighbor)->halite;

                double distance = std::sqrt(dx * dx + dy * dy);
                double weight = std::pow(decay, distance);

                sum += halite * weight;
                total_weight += weight;
            }
        }

        return (total_weight > 0) ? sum / total_weight : 0.0;
    }

    void AnnotationMap::calculate_attraction_field(const Game& game) {
        const double THRESHOLD = 290.0;

        for (int y = 0; y < map_->height; ++y) {
            for (int x = 0; x < map_->width; ++x) {
                double blurred = apply_exponential_blur(x, y, 0.75);
                annotations_[y][x].attraction = (blurred > THRESHOLD) ? blurred : 0.0;
            }
        }
    }

    void AnnotationMap::calculate_inspiration(const Game& game) {
        const int INSPIRATION_RADIUS = constants::INSPIRATION_RADIUS;
        const int INSPIRATION_SHIP_COUNT = constants::INSPIRATION_SHIP_COUNT;

        for (int y = 0; y < map_->height; ++y) {
            for (int x = 0; x < map_->width; ++x) {
                Position center(x, y);

                int enemy_count = 0;

                for (const auto& player : game.players) {
                    if (player->id == game.me->id) continue;

                    for (const auto& ship_pair : player->ships) {
                        if (manhattan_distance(center, ship_pair.second->position) <= INSPIRATION_RADIUS) {
                            enemy_count++;
                        }
                    }
                }

                annotations_[y][x].is_inspired = (enemy_count >= INSPIRATION_SHIP_COUNT);
            }
        }
    }

    void AnnotationMap::mark_enemy_reach(const Game& game) {
        for (const auto& player : game.players) {
            if (player->id == game.me->id) continue;

            for (const auto& ship_pair : player->ships) {
                Position enemy_pos = ship_pair.second->position;

                /// Mark neighbouring cells reachable by enemy
                for (const Position& neighbor : get_neighbors(enemy_pos)) {
                    annotations_[neighbor.y][neighbor.x].enemy_can_reach_next_turn = true;
                }
            }
        }
    }

    void AnnotationMap::mark_path(const std::vector<Position>& path, bool will_mine_at_end) {
        for (size_t i = 0; i < path.size() && i < 64; ++i) {
            const Position& pos = path[i];
            annotations_[pos.y][pos.x].occupied_turns |= (1ULL << i);
        }

        if (will_mine_at_end && !path.empty()) {
            const Position& end = path.back();
            annotations_[end.y][end.x].will_be_mined = true;
        }
    }

    bool AnnotationMap::is_occupied_at_turn(const Position& pos, int turn_offset) const {
        if (turn_offset >= 64) return false;
        return (annotations_[pos.y][pos.x].occupied_turns & (1ULL << turn_offset)) != 0;
    }

    std::set<Direction> AnnotationMap::get_forbidden_moves(const std::shared_ptr<Ship>& ship, const Game& game) const {
        std::set<Direction> forbidden;

        Position current = ship->position;
        int num_players = static_cast<int>(game.players.size());
        int dominance_threshold = (num_players == 2) ? -1 : 0;

        /// Test every direction
        std::vector<Direction> all_directions = {
            Direction::NORTH, Direction::SOUTH, Direction::EAST, Direction::WEST, Direction::STILL
        };

        for (Direction dir : all_directions) {
            Position target = current;

            if (dir != Direction::STILL) {
                switch (dir) {
                case Direction::NORTH: target.y--; break;
                case Direction::SOUTH: target.y++; break;
                case Direction::EAST:  target.x++; break;
                case Direction::WEST:  target.x--; break;
                default: break;
                }
                target = map_->normalize(target);
            }

            const CellAnnotation& cell = at(target);

            /// Forbidden if enemy can reach & low dominance
            if (cell.enemy_can_reach_next_turn && cell.dominance <= dominance_threshold) {
                forbidden.insert(dir);
            }
        }

        /// If all moves are forbidden except STILL, allow movement into highest dominance cell
        if (forbidden.size() >= 4) {
            /// Find neighbours with highest dominance
            Direction best_dir = Direction::NORTH;
            int best_dominance = -999;

            for (Direction dir : {Direction::NORTH, Direction::SOUTH, Direction::EAST, Direction::WEST}) {
                Position target = current;
                switch (dir) {
                case Direction::NORTH: target.y--; break;
                case Direction::SOUTH: target.y++; break;
                case Direction::EAST:  target.x++; break;
                case Direction::WEST:  target.x--; break;
                default: break;
                }
                target = map_->normalize(target);

                int dom = at(target).dominance;
                if (dom > best_dominance) {
                    best_dominance = dom;
                    best_dir = dir;
                }
            }

            forbidden.clear();
            // Keep only if allowed
            for (Direction dir : all_directions) {
                if (dir != best_dir && dir != Direction::STILL) {
                    forbidden.insert(dir);
                }
            }
        }

        return forbidden;
    }

} // namespace hlt