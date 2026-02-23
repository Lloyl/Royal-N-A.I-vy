#include "planning_strategy.hpp"
#include "constants.hpp"
#include <algorithm>
#include <queue>
#include <cmath>
#include <limits>
#include <functional>

namespace hlt {

    PlanningStrategy::PlanningStrategy() {}

    std::unordered_map<EntityId, Direction> PlanningStrategy::plan_turn(
        GameMap& game_map,
        AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        const std::vector<std::shared_ptr<Player>>& players,
        int turn_number,
        int num_players
    ) {
        std::unordered_map<EntityId, Direction> planned_moves;

        // Collect all ships with their candidate paths
        struct ShipWithPaths {
            EntityId ship_id;
            std::vector<PlannedPath> candidates;
        };
        std::vector<ShipWithPaths> all_ship_plans;

        // C++14: No structured bindings
        for (auto it = me->ships.begin(); it != me->ships.end(); ++it) {
            EntityId ship_id = it->first;
            std::shared_ptr<Ship> ship = it->second;
            auto candidates = generate_candidate_paths(ship, game_map, annotation_map, me, players, turn_number, num_players);
            ShipWithPaths swp;
            swp.ship_id = ship_id;
            swp.candidates = candidates;
            all_ship_plans.push_back(swp);
        }

        // Sort ships by best candidate score (greedy assignment)
        std::sort(all_ship_plans.begin(), all_ship_plans.end(),
            [](const ShipWithPaths& a, const ShipWithPaths& b) {
                float best_a = a.candidates.empty() ? -1e9f : a.candidates[0].score;
                float best_b = b.candidates.empty() ? -1e9f : b.candidates[0].score;
                return best_a > best_b;
            });

        // Assign paths greedily
        for (size_t i = 0; i < all_ship_plans.size(); ++i) {
            ShipWithPaths& ship_plan = all_ship_plans[i];
            bool assigned = false;

            for (size_t j = 0; j < ship_plan.candidates.size(); ++j) {
                PlannedPath& candidate = ship_plan.candidates[j];
                if (!has_path_conflict(candidate, annotation_map)) {
                    // Assign this path
                    if (!candidate.moves.empty()) {
                        planned_moves[ship_plan.ship_id] = candidate.moves[0];
                    }
                    else {
                        planned_moves[ship_plan.ship_id] = Direction::STILL;
                    }

                    // Mark path in annotation map
                    annotation_map.mark_path(candidate.positions, candidate.should_mine_at_end);
                    annotation_map.recalculate_forbidden_moves(game_map, me->id, num_players);

                    assigned = true;
                    break;
                }
            }

            if (!assigned) {
                // Fallback to STILL
                planned_moves[ship_plan.ship_id] = Direction::STILL;
            }
        }

        return planned_moves;
    }

    PlanningStrategy::ShipState& PlanningStrategy::get_ship_state(EntityId ship_id) {
        return ship_states_[ship_id];
    }

    std::vector<PlannedPath> PlanningStrategy::generate_candidate_paths(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        const std::vector<std::shared_ptr<Player>>& players,
        int turn_number,
        int num_players
    ) {
        ShipState& state = get_ship_state(ship->id);

        // Try roles in priority order
        std::vector<PlannedPath> candidates;

        // 1. End game return (highest priority)
        if (turn_number >= constants::MAX_TURNS - 100) {
            candidates = plan_end_game_return(ship, game_map, annotation_map, me, turn_number);
            if (!candidates.empty()) return candidates;
        }

        // 2. Returning (if already flagged or cargo > 990)
        if (state.is_returning || ship->halite > 990) {
            state.is_returning = true;
            candidates = plan_return(ship, game_map, annotation_map, me);
            if (!candidates.empty()) {
                // Check if we reached dropoff
                Position dropoff_pos = find_nearest_dropoff(ship->position, me, game_map);
                if (ship->position == dropoff_pos) {
                    state.is_returning = false; // Reset state
                }
                return candidates;
            }
        }

        // 3. Global mining
        if (ship->halite < 800) {
            candidates = plan_global_mining(ship, game_map, annotation_map);
            if (!candidates.empty()) return candidates;
        }

        // 4. Local mining
        candidates = plan_local_mining(ship, game_map, annotation_map, num_players);
        if (!candidates.empty()) return candidates;



        // 5. Greedy mining (with attack)
        candidates = plan_greedy_mining(ship, game_map, annotation_map, num_players);
        if (!candidates.empty()) return candidates;

        // 6. Last resort
        return plan_last_resort(ship, game_map, annotation_map, me);
    }

    std::vector<PlannedPath> PlanningStrategy::plan_end_game_return(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me,
        int turn_number
    ) {
        int turns_remaining = constants::MAX_TURNS - turn_number;
        Position dropoff = find_nearest_dropoff(ship->position, me, game_map);

        PathfindResult path_result = find_path_dijkstra(ship->position, dropoff, ship, game_map, annotation_map, true, true);

        if (path_result.found && path_result.path.estimated_turns <= turns_remaining) {
            path_result.path.score = 1000.0f - static_cast<float>(path_result.path.estimated_turns);
            std::vector<PlannedPath> result;
            result.push_back(path_result.path);
            return result;
        }

        return std::vector<PlannedPath>();
    }

    std::vector<PlannedPath> PlanningStrategy::plan_return(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me
    ) {
        Position dropoff = find_nearest_dropoff(ship->position, me, game_map);

        PathfindResult path_result = find_path_dijkstra(ship->position, dropoff, ship, game_map, annotation_map);

        if (path_result.found) {
            // High priority for returning
            path_result.path.score = 500.0f - static_cast<float>(path_result.path.estimated_turns);
            std::vector<PlannedPath> result;
            result.push_back(path_result.path);
            return result;
        }

        return std::vector<PlannedPath>();
    }

    std::vector<PlannedPath> PlanningStrategy::plan_local_mining(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        int num_players
    ) {
        // Parameters vary by player count
        int min_energy, min_cargo, max_dist;
        float time_penalty;

        if (num_players == 2) {
            min_energy = 85;
            min_cargo = 390;
            max_dist = 3;
            time_penalty = 17.5f;
        }
        else {
            min_energy = 128;
            min_cargo = 562;
            max_dist = 3;
            time_penalty = 30.0f;
        }

        return depth_first_local_mining(ship->position, ship, game_map, annotation_map,
            max_dist, min_energy, min_cargo, time_penalty);
    }

    std::vector<PlannedPath> PlanningStrategy::plan_global_mining(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map
    ) {
        // Use Dijkstra to explore entire map
        struct Node {
            Position pos;
            float cost;
            std::vector<Position> path;
            Halite ship_halite;

            bool operator>(const Node& other) const {
                return cost > other.cost;
            }
        };

        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        std::unordered_map<Position, float> visited;

        Node start_node;
        start_node.pos = ship->position;
        start_node.cost = 0.0f;
        start_node.path.push_back(ship->position);
        start_node.ship_halite = ship->halite;
        pq.push(start_node);

        std::vector<PlannedPath> candidates;

        while (!pq.empty()) {
            Node current = pq.top();
            pq.pop();

            if (visited.count(current.pos) && visited[current.pos] <= current.cost) {
                continue;
            }
            visited[current.pos] = current.cost;

            // Evaluate this position as mining target
            const AnnotationMap::CellInfo& cell_info = annotation_map.at(current.pos);
            if (cell_info.attraction > 0 && !cell_info.marked_for_mining) {
                PlannedPath candidate;
                candidate.positions = current.path;
                candidate.score = cell_info.attraction - current.cost * 30.0f; // time_penalty
                candidate.should_mine_at_end = true;
                candidate.estimated_turns = static_cast<int>(current.cost);
                candidate.estimated_cargo = current.ship_halite;

                // Generate moves from path
                for (size_t i = 1; i < current.path.size(); ++i) {
                    std::vector<Direction> dirs = game_map.get_unsafe_moves(current.path[i - 1], current.path[i]);
                    if (!dirs.empty()) {
                        candidate.moves.push_back(dirs[0]);
                    }
                }

                candidates.push_back(candidate);
            }

            // Explore neighbors
            for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                Direction dir = ALL_CARDINALS[d];
                Position next_pos = game_map.normalize(current.pos.directional_offset(dir));

                if (annotation_map.is_move_forbidden(current.pos, dir)) continue;

                float move_cost = calculate_move_cost(current.pos, next_pos, ship, game_map, annotation_map);

                std::vector<Position> new_path = current.path;
                new_path.push_back(next_pos);

                Node next_node;
                next_node.pos = next_pos;
                next_node.cost = current.cost + move_cost;
                next_node.path = new_path;
                next_node.ship_halite = current.ship_halite;
                pq.push(next_node);
            }
        }

        // Sort by score
        std::sort(candidates.begin(), candidates.end(),
            [](const PlannedPath& a, const PlannedPath& b) { return a.score > b.score; });

        return candidates;
    }

    std::vector<PlannedPath> PlanningStrategy::plan_greedy_mining(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        int num_players
    ) {
        float inspired_factor = (num_players == 2) ? 1.7f : 8.0f;

        // Similar to global mining but with different scoring
        struct Node {
            Position pos;
            float cost;
            std::vector<Position> path;

            bool operator>(const Node& other) const {
                return cost > other.cost;
            }
        };

        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        std::unordered_map<Position, float> visited;

        Node start_node;
        start_node.pos = ship->position;
        start_node.cost = 0.0f;
        start_node.path.push_back(ship->position);
        pq.push(start_node);

        std::vector<PlannedPath> candidates;

        while (!pq.empty()) {
            Node current = pq.top();
            pq.pop();

            if (visited.count(current.pos) && visited[current.pos] <= current.cost) {
                continue;
            }
            visited[current.pos] = current.cost;

            // Evaluate this position
            const AnnotationMap::CellInfo& cell_info = annotation_map.at(current.pos);
            const MapCell* map_cell = game_map.at(current.pos);

            Halite energy = map_cell->halite;
            if (cell_info.is_inspired) {
                energy = static_cast<Halite>(energy * inspired_factor);
            }

            // Include enemy cargo if attacking
            if (cell_info.has_enemy_ship && cell_info.dominance_score > 0) {
                if (map_cell->ship) {
                    energy += map_cell->ship->halite;
                }
            }

            int max_collectible = std::min(energy, constants::MAX_HALITE - ship->halite) * 4;
            float score = static_cast<float>(max_collectible) / (current.cost + 1.0f);

            PlannedPath candidate;
            candidate.positions = current.path;
            candidate.score = score;
            candidate.should_mine_at_end = true;
            candidate.estimated_turns = static_cast<int>(current.cost);

            for (size_t i = 1; i < current.path.size(); ++i) {
                std::vector<Direction> dirs = game_map.get_unsafe_moves(current.path[i - 1], current.path[i]);
                if (!dirs.empty()) {
                    candidate.moves.push_back(dirs[0]);
                }
            }

            candidates.push_back(candidate);

            // Explore neighbors
            for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                Direction dir = ALL_CARDINALS[d];
                Position next_pos = game_map.normalize(current.pos.directional_offset(dir));

                if (annotation_map.is_move_forbidden(current.pos, dir)) continue;

                float move_cost = calculate_move_cost(current.pos, next_pos, ship, game_map, annotation_map);

                std::vector<Position> new_path = current.path;
                new_path.push_back(next_pos);

                Node next_node;
                next_node.pos = next_pos;
                next_node.cost = current.cost + move_cost;
                next_node.path = new_path;
                pq.push(next_node);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
            [](const PlannedPath& a, const PlannedPath& b) { return a.score > b.score; });

        return candidates;
    }

    std::vector<PlannedPath> PlanningStrategy::plan_last_resort(
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        const std::shared_ptr<Player>& me
    ) {
        PlannedPath fallback;
        fallback.positions.push_back(ship->position);
        fallback.moves.push_back(Direction::STILL);
        fallback.score = 0.0f;
        fallback.should_mine_at_end = false;
        fallback.estimated_turns = 1;

        // If cargo is sufficient, try to return
        if (ship->halite >= 800) {
            return plan_return(ship, game_map, annotation_map, me);
        }

        std::vector<PlannedPath> result;
        result.push_back(fallback);
        return result;
    }

    PathfindResult PlanningStrategy::find_path_dijkstra(
        const Position& start,
        const Position& goal,
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        bool allow_waiting,
        bool is_end_game_return
    ) {
        struct Node {
            Position pos;
            float cost;
            int turn;
            Halite ship_cargo;
            std::vector<Position> path;

            bool operator>(const Node& other) const {
                return cost > other.cost;
            }
        };

        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        std::unordered_map<Position, float> visited;

        Node start_node;
        start_node.pos = start;
        start_node.cost = 0.0f;
        start_node.turn = 0;
        start_node.ship_cargo = ship->halite;
        start_node.path.push_back(start);
        pq.push(start_node);

        while (!pq.empty()) {
            Node current = pq.top();
            pq.pop();

            if (current.pos == goal) {
                // Found path
                PlannedPath result;
                result.positions = current.path;
                result.estimated_turns = current.turn;
                result.estimated_cargo = current.ship_cargo;
                result.score = 0.0f;
                result.should_mine_at_end = false;

                // Generate moves
                for (size_t i = 1; i < current.path.size(); ++i) {
                    std::vector<Direction> dirs = game_map.get_unsafe_moves(current.path[i - 1], current.path[i]);
                    if (!dirs.empty()) {
                        result.moves.push_back(dirs[0]);
                    }
                }

                return PathfindResult(result);
            }

            if (visited.count(current.pos) && visited[current.pos] <= current.cost) {
                continue;
            }
            visited[current.pos] = current.cost;

            // Try moving to neighbors
            for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                Direction dir = ALL_CARDINALS[d];
                Position next_pos = game_map.normalize(current.pos.directional_offset(dir));

                if (annotation_map.is_move_forbidden(current.pos, dir)) continue;

                // Check if cell is occupied
                const AnnotationMap::CellInfo& cell_info = annotation_map.at(next_pos);
                bool is_occupied = (cell_info.occupation_timeline & (1ULL << current.turn)) != 0;

                if (is_occupied && !allow_waiting) continue;

                float move_cost = calculate_move_cost(current.pos, next_pos, ship, game_map, annotation_map, is_end_game_return);

                std::vector<Position> new_path = current.path;
                new_path.push_back(next_pos);

                Node next_node;
                next_node.pos = next_pos;
                next_node.cost = current.cost + move_cost;
                next_node.turn = current.turn + 1;
                next_node.ship_cargo = current.ship_cargo;
                next_node.path = new_path;
                pq.push(next_node);
            }

            // Try waiting (if allowed)
            if (allow_waiting) {
                Node wait_node;
                wait_node.pos = current.pos;
                wait_node.cost = current.cost + 1.0f;
                wait_node.turn = current.turn + 1;
                wait_node.ship_cargo = current.ship_cargo;
                wait_node.path = current.path;
                pq.push(wait_node);
            }
        }

        return PathfindResult(); // Not found
    }

    std::vector<PlannedPath> PlanningStrategy::depth_first_local_mining(
        const Position& start,
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        int max_dist,
        int min_energy,
        int min_cargo,
        float time_penalty
    ) {
        std::vector<PlannedPath> candidates;

        // DFS helper using manual stack instead of recursion for C++14
        struct DFSState {
            Position pos;
            int depth;
            Halite cargo;
            float cost;
            std::vector<Position> path;
        };

        std::vector<DFSState> stack;
        DFSState initial;
        initial.pos = start;
        initial.depth = 0;
        initial.cargo = ship->halite;
        initial.cost = 0.0f;
        initial.path.push_back(start);
        stack.push_back(initial);

        while (!stack.empty()) {
            DFSState current = stack.back();
            stack.pop_back();

            if (current.depth > max_dist) continue;

            // Try mining at current position
            const MapCell* cell = game_map.at(current.pos);
            const AnnotationMap::CellInfo& cell_info = annotation_map.at(current.pos);

            if (cell->halite >= min_energy && !cell_info.marked_for_mining) {
                std::pair<Halite, int> mining_result = simulate_mining(
                    cell_info.remaining_halite, current.cargo, min_energy, cell_info.is_inspired
                );
                Halite halite_collected = mining_result.first;
                int turns = mining_result.second;

                if (current.cargo + halite_collected >= min_cargo) {
                    PlannedPath candidate;
                    candidate.positions = current.path;
                    candidate.score = static_cast<float>(halite_collected) - current.cost - time_penalty * turns;
                    candidate.should_mine_at_end = true;
                    candidate.estimated_turns = static_cast<int>(current.cost) + turns;
                    candidate.estimated_cargo = current.cargo + halite_collected;

                    for (size_t i = 1; i < current.path.size(); ++i) {
                        std::vector<Direction> dirs = game_map.get_unsafe_moves(current.path[i - 1], current.path[i]);
                        if (!dirs.empty()) {
                            candidate.moves.push_back(dirs[0]);
                        }
                    }

                    candidates.push_back(candidate);
                }
            }

            // Explore neighbors
            if (current.depth < max_dist) {
                for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                    Direction dir = ALL_CARDINALS[d];
                    Position next_pos = game_map.normalize(current.pos.directional_offset(dir));

                    if (annotation_map.is_move_forbidden(current.pos, dir)) continue;

                    float move_cost = calculate_move_cost(current.pos, next_pos, ship, game_map, annotation_map);

                    DFSState next_state;
                    next_state.pos = next_pos;
                    next_state.depth = current.depth + 1;
                    next_state.cargo = current.cargo;
                    next_state.cost = current.cost + move_cost;
                    next_state.path = current.path;
                    next_state.path.push_back(next_pos);

                    stack.push_back(next_state);
                }
            }
        }

        // Sort by score
        std::sort(candidates.begin(), candidates.end(),
            [](const PlannedPath& a, const PlannedPath& b) { return a.score > b.score; });

        return candidates;
    }

    float PlanningStrategy::calculate_move_cost(
        const Position& from,
        const Position& to,
        const std::shared_ptr<Ship>& ship,
        GameMap& game_map,
        const AnnotationMap& annotation_map,
        bool is_end_game_return
    ) {
        // Base cost: 1 turn
        float cost = 1.0f;

        // Halite usage penalty
        const MapCell* from_cell = game_map.at(from);
        int move_cost_halite = from_cell->halite / constants::MOVE_COST_RATIO;
        if (ship->halite < move_cost_halite) {
            // Ship needs to wait and mine
            cost += 1.0f; // Additional turn to gather halite
        }
        cost += static_cast<float>(move_cost_halite) / 1000.0f;

        // Dominance penalty
        const AnnotationMap::CellInfo& to_info = annotation_map.at(to);
        if (to_info.dominance_score <= -5) {
            float penalty_dominance = 3.2f;
            cost += static_cast<float>(-to_info.dominance_score) / 5.0f * penalty_dominance;
        }

        // Enemy proximity penalty
        if (to_info.distance_to_nearest_enemy <= 3) {
            cost += 3.5f;
        }

        // End game specific penalties
        if (is_end_game_return) {
            // Check if first move has enemy adjacent
            if (from == ship->position) {
                bool enemy_adjacent = false;
                for (size_t d = 0; d < ALL_CARDINALS.size(); ++d) {
                    Direction dir = ALL_CARDINALS[d];
                    Position adj = game_map.normalize(from.directional_offset(dir));
                    if (annotation_map.at(adj).has_enemy_ship) {
                        enemy_adjacent = true;
                        break;
                    }
                }
                if (enemy_adjacent) {
                    cost += 10.0f;
                }
            }
        }

        // Dropoff penalty (avoid congestion)
        const MapCell* to_cell = game_map.at(to);
        if (to_cell->has_structure()) {
            cost += 3.0f;
        }

        return cost;
    }

    bool PlanningStrategy::has_path_conflict(
        const PlannedPath& path,
        const AnnotationMap& annotation_map
    ) const {
        // Check if any position in path is already occupied at that turn
        for (size_t i = 0; i < path.positions.size() && i < 64; ++i) {
            const AnnotationMap::CellInfo& cell_info = annotation_map.at(path.positions[i]);

            // Check occupation timeline
            if ((cell_info.occupation_timeline & (1ULL << i)) != 0) {
                return true;
            }

            // Check if mining target is already marked
            if (i == path.positions.size() - 1 && path.should_mine_at_end) {
                if (cell_info.marked_for_mining) {
                    return true;
                }
            }
        }

        return false;
    }

    std::pair<Halite, int> PlanningStrategy::simulate_mining(
        Halite cell_halite,
        Halite ship_cargo,
        int threshold,
        bool is_inspired
    ) const {
        int extract_ratio = is_inspired ? constants::INSPIRED_EXTRACT_RATIO : constants::EXTRACT_RATIO;

        Halite total_collected = 0;
        int turns = 0;
        Halite current_cell = cell_halite;
        Halite current_cargo = ship_cargo;

        while (current_cell >= threshold && current_cargo < constants::MAX_HALITE) {
            Halite collected = current_cell / extract_ratio;

            if (is_inspired) {
                collected = static_cast<Halite>(collected * constants::INSPIRED_BONUS_MULTIPLIER);
            }

            collected = std::min(collected, constants::MAX_HALITE - current_cargo);

            total_collected += collected;
            current_cargo += collected;
            current_cell -= (cell_halite / extract_ratio); // Remove base amount from cell
            turns++;

            if (current_cargo >= constants::MAX_HALITE) break;
        }

        return std::make_pair(total_collected, turns);
    }

    Position PlanningStrategy::find_nearest_dropoff(
        const Position& pos,
        const std::shared_ptr<Player>& me,
        GameMap& game_map
    ) const {
        Position nearest = me->shipyard->position;
        int min_dist = game_map.calculate_distance(pos, nearest);

        for (auto it = me->dropoffs.begin(); it != me->dropoffs.end(); ++it) {
            const std::shared_ptr<Dropoff>& dropoff = it->second;
            int dist = game_map.calculate_distance(pos, dropoff->position);
            if (dist < min_dist) {
                min_dist = dist;
                nearest = dropoff->position;
            }
        }

        return nearest;
    }

} // namespace hlt