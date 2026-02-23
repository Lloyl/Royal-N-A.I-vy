#pragma once

#include "types.hpp"
#include "position.hpp"
#include "direction.hpp"
#include "game_map.hpp"
#include "player.hpp"

#include <vector>
#include <unordered_map>
#include <bitset>
#include <memory>

namespace hlt {

    /**
     * @brief Stores precomputed strategic information about the current game state.
     *
     * The AnnotationMap is built once per turn and provides quick access to:
     * - Attraction fields (blurred halite density)
     * - Dominance areas (friendly vs enemy ship count)
     * - Forbidden moves (risky movements near enemies)
     * - Path markings (reserved cells and mining targets)
     * - Inspiration status
     * - Enemy presence and accessibility
     * - Distance to nearest allies, enemies, and dropoffs
     */
    class AnnotationMap {
    public:
        /**
         * @brief Information stored for each cell on the map.
         */
        struct CellInfo {
            float attraction;              ///< Attraction field value after blur and threshold
            int allied_dominance;          ///< Number of allied ships in dominance radius
            int enemy_dominance;           ///< Number of enemy ships in dominance radius
            int dominance_score;           ///< allied_dominance - enemy_dominance
            std::bitset<5> forbidden_moves; ///< Flags for each direction (NORTH, SOUTH, EAST, WEST, STILL)
            uint64_t occupation_timeline;   ///< Bitfield indicating occupied turns in next 64 turns
            bool marked_for_mining;         ///< True if a ship has targeted this cell for mining
            bool is_inspired;               ///< True if cell provides inspiration bonus
            bool has_enemy_ship;            ///< True if an enemy ship is on this cell
            bool enemy_accessible_next_turn; ///< True if enemy can reach this cell next turn
            int distance_to_nearest_ally;   ///< Manhattan distance to nearest allied ship
            int distance_to_nearest_enemy;  ///< Manhattan distance to nearest enemy ship
            int distance_to_nearest_dropoff; ///< Manhattan distance to nearest allied dropoff
            Halite remaining_halite;        ///< Halite remaining after accounting for planned mining

            CellInfo() :
                attraction(0.0f),
                allied_dominance(0),
                enemy_dominance(0),
                dominance_score(0),
                forbidden_moves(0),
                occupation_timeline(0),
                marked_for_mining(false),
                is_inspired(false),
                has_enemy_ship(false),
                enemy_accessible_next_turn(false),
                distance_to_nearest_ally(999),
                distance_to_nearest_enemy(999),
                distance_to_nearest_dropoff(999),
                remaining_halite(0)
            {
            }
        };

        /**
         * @brief Constructs an empty annotation map.
         * @param width Map width
         * @param height Map height
         */
        AnnotationMap(int width, int height);

        /**
         * @brief Builds the complete annotation map from current game state.
         * @param game_map The current game map
         * @param players All players in the game
         * @param my_id The current player's ID
         * @param turn_number Current turn number
         */
        void build(GameMap& game_map,
            const std::vector<std::shared_ptr<Player>>& players,
            PlayerId my_id,
            int turn_number);

        /**
         * @brief Gets cell information at a specific position.
         * @param pos Position to query
         * @return Reference to cell info
         */
        CellInfo& at(const Position& pos);

        /**
         * @brief Gets cell information at a specific position (const version).
         * @param pos Position to query
         * @return Const reference to cell info
         */
        const CellInfo& at(const Position& pos) const;

        /**
         * @brief Marks a path as occupied and optionally marks destination for mining.
         * @param path Vector of positions representing the path
         * @param mark_last_for_mining If true, marks the last position as a mining target
         * @param turn_offset Turn offset for occupation timeline
         */
        void mark_path(const std::vector<Position>& path,
            bool mark_last_for_mining,
            int turn_offset = 0);

        /**
         * @brief Checks if a move is forbidden for a ship at given position.
         * @param pos Ship's current position
         * @param direction Direction to check
         * @return True if the move is forbidden
         */
        bool is_move_forbidden(const Position& pos, Direction direction) const;

        /**
         * @brief Sets a move as forbidden for a specific position.
         * @param pos Position to update
         * @param direction Direction to forbid
         */
        void set_move_forbidden(const Position& pos, Direction direction);

        /**
         * @brief Recalculates forbidden moves after path marking.
         * @param game_map The current game map
         * @param my_id The current player's ID
         * @param num_players Total number of players in game
         */
        void recalculate_forbidden_moves(GameMap& game_map, PlayerId my_id, int num_players);

    private:
        int width_;
        int height_;
        std::vector<std::vector<CellInfo>> cells_;

        /**
         * @brief Normalizes position for toroidal map.
         */
        Position normalize(const Position& pos) const;

        /**
         * @brief Builds the attraction field using exponential blur and threshold.
         * @param game_map The current game map
         * @param blur_factor Exponential blur factor (default 0.75)
         * @param threshold Minimum halite threshold (default 290)
         */
        void build_attraction_field(GameMap& game_map,
            float blur_factor = 0.75f,
            int threshold = 290);

        /**
         * @brief Calculates dominance areas for each cell.
         * @param game_map The current game map
         * @param players All players in the game
         * @param my_id The current player's ID
         * @param dominance_radius Radius for dominance calculation (default 5)
         */
        void calculate_dominance(GameMap& game_map,
            const std::vector<std::shared_ptr<Player>>& players,
            PlayerId my_id,
            int dominance_radius = 5);

        /**
         * @brief Marks cells with inspiration status.
         * @param game_map The current game map
         * @param players All players in the game
         * @param my_id The current player's ID
         */
        void calculate_inspiration(GameMap& game_map,
            const std::vector<std::shared_ptr<Player>>& players,
            PlayerId my_id);

        /**
         * @brief Marks enemy ship presence and accessible cells.
         * @param game_map The current game map
         * @param players All players in the game
         * @param my_id The current player's ID
         */
        void mark_enemy_presence(GameMap& game_map,
            const std::vector<std::shared_ptr<Player>>& players,
            PlayerId my_id);

        /**
         * @brief Calculates distances to nearest allies, enemies, and dropoffs.
         * @param game_map The current game map
         * @param players All players in the game
         * @param my_id The current player's ID
         */
        void calculate_distances(GameMap& game_map,
            const std::vector<std::shared_ptr<Player>>& players,
            PlayerId my_id);

        /**
         * @brief Initializes forbidden moves based on enemy proximity and dominance.
         * @param game_map The current game map
         * @param my_id The current player's ID
         * @param num_players Total number of players
         */
        void initialize_forbidden_moves(GameMap& game_map, PlayerId my_id, int num_players);

        /**
         * @brief Gets direction index for bitset access.
         */
        int get_direction_index(Direction dir) const;
    };

} // namespace hlt