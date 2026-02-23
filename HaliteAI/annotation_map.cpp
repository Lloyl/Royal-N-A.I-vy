#include "annotation_map.hpp"
#include "constants.hpp"
#include <cmath>
#include <algorithm>
#include <queue>

namespace hlt {

    AnnotationMap::AnnotationMap(int width, int height)
        : width_(width), height_(height) {
        cells_.resize(height, std::vector<CellInfo>(width));
    }

    void AnnotationMap::build(GameMap& game_map,
        const std::vector<std::shared_ptr<Player>>& players,
        PlayerId my_id,
        int turn_number) {
        // Reset all cells
        for (auto& row : cells_) {
            for (auto& cell : row) {
                cell = CellInfo();
            }
        }

        // Initialize remaining halite from current map state
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                cells_[y][x].remaining_halite = game_map.at(Position(x, y))->halite;
            }
        }

        // Build all annotation layers
        build_attraction_field(game_map);
        calculate_dominance(game_map, players, my_id);
        calculate_inspiration(game_map, players, my_id);
        mark_enemy_presence(game_map, players, my_id);
        calculate_distances(game_map, players, my_id);
        initialize_forbidden_moves(game_map, my_id, static_cast<int>(players.size()));
    }

    AnnotationMap::CellInfo& AnnotationMap::at(const Position& pos) {
        Position norm = normalize(pos);
        return cells_[norm.y][norm.x];
    }

    const AnnotationMap::CellInfo& AnnotationMap::at(const Position& pos) const {
        Position norm = normalize(pos);
        return cells_[norm.y][norm.x];
    }

    Position AnnotationMap::normalize(const Position& pos) const {
        int x = ((pos.x % width_) + width_) % width_;
        int y = ((pos.y % height_) + height_) % height_;
        return Position(x, y);
    }

    void AnnotationMap::build_attraction_field(GameMap& game_map,
        float blur_factor,
        int threshold) {
        // Step 1: Copy halite values
        std::vector<std::vector<float>> halite_map(height_, std::vector<float>(width_));
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                halite_map[y][x] = static_cast<float>(game_map.at(Position(x, y))->halite);
            }
        }

        // Step 2: Apply exponential blur
        std::vector<std::vector<float>> blurred(height_, std::vector<float>(width_, 0.0f));
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                float sum = 0.0f;
                float weight_sum = 0.0f;

                // Apply blur in a small radius (e.g., 3x3)
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        Position neighbor = normalize(Position(x + dx, y + dy));
                        float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        float weight = std::pow(blur_factor, distance);
                        sum += halite_map[neighbor.y][neighbor.x] * weight;
                        weight_sum += weight;
                    }
                }

                blurred[y][x] = sum / weight_sum;
            }
        }

        // Step 3: Apply threshold
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                cells_[y][x].attraction = blurred[y][x] >= threshold ? blurred[y][x] : 0.0f;
            }
        }
    }

    void AnnotationMap::calculate_dominance(GameMap& game_map,
        const std::vector<std::shared_ptr<Player>>& players,
        PlayerId my_id,
        int dominance_radius) {
        // Count ships in radius for each cell
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Position center(x, y);
                int allied_count = 0;
                int enemy_count = 0;

                // Check all cells in radius
                for (int dy = -dominance_radius; dy <= dominance_radius; ++dy) {
                    for (int dx = -dominance_radius; dx <= dominance_radius; ++dx) {
                        int manhattan_dist = std::abs(dx) + std::abs(dy);
                        if (manhattan_dist > dominance_radius) continue;

                        Position check_pos = normalize(Position(x + dx, y + dy));
                        const auto* cell = game_map.at(check_pos);

                        if (cell->ship) {
                            if (cell->ship->owner == my_id) {
                                allied_count++;
                            }
                            else {
                                enemy_count++;
                            }
                        }
                    }
                }

                cells_[y][x].allied_dominance = allied_count;
                cells_[y][x].enemy_dominance = enemy_count;
                cells_[y][x].dominance_score = allied_count - enemy_count;
            }
        }
    }

    void AnnotationMap::calculate_inspiration(GameMap& game_map,
        const std::vector<std::shared_ptr<Player>>& players,
        PlayerId my_id) {
        if (!constants::INSPIRATION_ENABLED) return;

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Position center(x, y);
                int enemy_count = 0;

                // Count enemy ships in inspiration radius
                for (int dy = -constants::INSPIRATION_RADIUS; dy <= constants::INSPIRATION_RADIUS; ++dy) {
                    for (int dx = -constants::INSPIRATION_RADIUS; dx <= constants::INSPIRATION_RADIUS; ++dx) {
                        int manhattan_dist = std::abs(dx) + std::abs(dy);
                        if (manhattan_dist > constants::INSPIRATION_RADIUS) continue;

                        Position check_pos = normalize(Position(x + dx, y + dy));
                        const auto* cell = game_map.at(check_pos);

                        if (cell->ship && cell->ship->owner != my_id) {
                            enemy_count++;
                        }
                    }
                }

                cells_[y][x].is_inspired = (enemy_count >= constants::INSPIRATION_SHIP_COUNT);
            }
        }
    }

    void AnnotationMap::mark_enemy_presence(GameMap& game_map,
        const std::vector<std::shared_ptr<Player>>& players,
        PlayerId my_id) {
        // Mark enemy ships
        for (size_t i = 0; i < players.size(); ++i) {
            const auto& player = players[i];
            if (player->id == my_id) continue;

            for (auto it = player->ships.begin(); it != player->ships.end(); ++it) {
                const auto& ship = it->second;
                Position pos = ship->position;
                Position norm = normalize(pos);
                cells_[norm.y][norm.x].has_enemy_ship = true;

                // Mark accessible cells
                for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                    Direction dir = ALL_CARDINALS[d];
                    Position adjacent = normalize(pos.directional_offset(dir));
                    cells_[adjacent.y][adjacent.x].enemy_accessible_next_turn = true;
                }
                // Enemy can also stay still
                cells_[norm.y][norm.x].enemy_accessible_next_turn = true;
            }
        }
    }

    void AnnotationMap::calculate_distances(GameMap& game_map,
        const std::vector<std::shared_ptr<Player>>& players,
        PlayerId my_id) {
        // Find my player
        std::shared_ptr<Player> me;
        for (size_t i = 0; i < players.size(); ++i) {
            if (players[i]->id == my_id) {
                me = players[i];
                break;
            }
        }
        if (!me) return;

        // Calculate distance to nearest ally, enemy, dropoff
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Position pos(x, y);
                int min_ally_dist = 999;
                int min_enemy_dist = 999;
                int min_dropoff_dist = 999;

                // Distance to allied ships
                for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
                    const auto& ship = it->second;
                    int wrap_x = std::min(std::abs(x - ship->position.x), width_ - std::abs(x - ship->position.x));
                    int wrap_y = std::min(std::abs(y - ship->position.y), height_ - std::abs(y - ship->position.y));
                    int dist = wrap_x + wrap_y;
                    min_ally_dist = std::min(min_ally_dist, dist);
                }

                // Distance to enemy ships
                for (size_t i = 0; i < players.size(); ++i) {
                    const auto& player = players[i];
                    if (player->id == my_id) continue;
                    for (auto it = player->ships.begin(); it != player->ships.end(); ++it) {
                        const auto& ship = it->second;
                        int wrap_x = std::min(std::abs(x - ship->position.x), width_ - std::abs(x - ship->position.x));
                        int wrap_y = std::min(std::abs(y - ship->position.y), height_ - std::abs(y - ship->position.y));
                        int dist = wrap_x + wrap_y;
                        min_enemy_dist = std::min(min_enemy_dist, dist);
                    }
                }

                // Distance to dropoffs (including shipyard)
                if (me->shipyard) {
                    int wrap_x = std::min(std::abs(x - me->shipyard->position.x), width_ - std::abs(x - me->shipyard->position.x));
                    int wrap_y = std::min(std::abs(y - me->shipyard->position.y), height_ - std::abs(y - me->shipyard->position.y));
                    min_dropoff_dist = wrap_x + wrap_y;
                }
                for (auto it = me->dropoffs.begin(); it != me->dropoffs.end(); ++it) {
                    const auto& dropoff = it->second;
                    int wrap_x = std::min(std::abs(x - dropoff->position.x), width_ - std::abs(x - dropoff->position.x));
                    int wrap_y = std::min(std::abs(y - dropoff->position.y), height_ - std::abs(y - dropoff->position.y));
                    int dist = wrap_x + wrap_y;
                    min_dropoff_dist = std::min(min_dropoff_dist, dist);
                }

                cells_[y][x].distance_to_nearest_ally = min_ally_dist;
                cells_[y][x].distance_to_nearest_enemy = min_enemy_dist;
                cells_[y][x].distance_to_nearest_dropoff = min_dropoff_dist;
            }
        }
    }

    void AnnotationMap::initialize_forbidden_moves(GameMap& game_map, PlayerId my_id, int num_players) {
        // Find my player's ships
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const auto* cell = game_map.at(Position(x, y));
                if (!cell->ship || cell->ship->owner != my_id) continue;

                Position ship_pos(x, y);
                const auto& info = at(ship_pos);

                int dominance_threshold = (num_players == 2) ? -1 : 0;

                // Check each direction
                Direction all_dirs[5] = { Direction::NORTH, Direction::SOUTH, Direction::EAST, Direction::WEST, Direction::STILL };
                for (int d = 0; d < 5; ++d) {
                    Direction dir = all_dirs[d];
                    Position target = normalize(ship_pos.directional_offset(dir));
                    const auto& target_info = at(target);

                    bool is_forbidden = false;

                    // Forbid if enemy can move there and dominance is bad
                    if (target_info.enemy_accessible_next_turn &&
                        target_info.dominance_score <= dominance_threshold) {
                        is_forbidden = true;
                    }

                    if (is_forbidden) {
                        set_move_forbidden(ship_pos, dir);
                    }
                }

                // If all moves forbidden, allow best non-STILL move
                bool all_forbidden = true;
                for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                    if (!is_move_forbidden(ship_pos, ALL_CARDINALS[d])) {
                        all_forbidden = false;
                        break;
                    }
                }

                if (all_forbidden && is_move_forbidden(ship_pos, Direction::STILL)) {
                    // Find best escape direction
                    Direction best_dir = Direction::NORTH;
                    int best_dominance = -999;

                    for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                        Direction dir = ALL_CARDINALS[d];
                        Position target = normalize(ship_pos.directional_offset(dir));
                        int dom = at(target).dominance_score;
                        if (dom > best_dominance) {
                            best_dominance = dom;
                            best_dir = dir;
                        }
                    }

                    // Clear all forbidden flags and only forbid STILL
                    cells_[y][x].forbidden_moves.reset();
                    set_move_forbidden(ship_pos, Direction::STILL);
                }
            }
        }
    }

    void AnnotationMap::mark_path(const std::vector<Position>& path,
        bool mark_last_for_mining,
        int turn_offset) {
        for (size_t i = 0; i < path.size() && i < 64; ++i) {
            Position norm = normalize(path[i]);
            int turn = turn_offset + static_cast<int>(i);
            if (turn < 64) {
                cells_[norm.y][norm.x].occupation_timeline |= (1ULL << turn);
            }
        }

        if (mark_last_for_mining && !path.empty()) {
            Position last = normalize(path.back());
            cells_[last.y][last.x].marked_for_mining = true;
        }
    }

    bool AnnotationMap::is_move_forbidden(const Position& pos, Direction direction) const {
        Position norm = normalize(pos);
        int idx = get_direction_index(direction);
        return cells_[norm.y][norm.x].forbidden_moves[idx];
    }

    void AnnotationMap::set_move_forbidden(const Position& pos, Direction direction) {
        Position norm = normalize(pos);
        int idx = get_direction_index(direction);
        cells_[norm.y][norm.x].forbidden_moves[idx] = true;
    }

    void AnnotationMap::recalculate_forbidden_moves(GameMap& game_map, PlayerId my_id, int num_players) {
        // Recalculate after path marking
        initialize_forbidden_moves(game_map, my_id, num_players);
    }

    int AnnotationMap::get_direction_index(Direction dir) const {
        switch (dir) {
        case Direction::NORTH: return 0;
        case Direction::SOUTH: return 1;
        case Direction::EAST: return 2;
        case Direction::WEST: return 3;
        case Direction::STILL: return 4;
        default: return 4;
        }
    }

} // namespace hlt