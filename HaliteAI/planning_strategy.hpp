#pragma once

/**
 * @file planning_strategy.hpp
 * @brief Assignation de rôles, pathfinding et système de candidats pour chaque ship.
 *
 * Ce module constitue le cœur décisionnel du bot. À chaque tour, il attribue
 * un rôle à chaque ship (end-game return, livraison, minage local, minage
 * global, minage greedy, dernier recours), calcule le meilleur chemin via
 * Dijkstra, et résout les conflits par un système de candidats triés par score.
 *
 * Les rôles sont traités séquentiellement par priorité. Un ship assigné à un
 * rôle n'est plus considéré pour les rôles suivants.
 */

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>
#include <algorithm>
#include <queue>
#include <cmath>

#include "types.hpp"
#include "direction.hpp"
#include "position.hpp"
#include "game.hpp"
#include "constants.hpp"

#include "annotation_map.hpp"

namespace hlt {

// ============================================================================
// Constantes de planification
// ============================================================================

/// Seuil de cargo pour déclencher la livraison
constexpr int RETURN_CARGO_THRESHOLD   = 600;

/// Seuil de cargo pour le dernier recours (last resort → return)
constexpr int LAST_RESORT_CARGO        = 500;

/// Seuil de cargo pour forcer le retour quand aucun minage local n'est rentable
constexpr int FORCE_RETURN_CARGO       = 400;

/// Marge en tours pour le retour end-game
constexpr int ENDGAME_BUFFER_TURNS     = 15;

/// Nombre max de nœuds explorés pour le Dijkstra global (minage global)
constexpr int MAX_NODES_GLOBAL         = 1000;

/// Nombre max de nœuds explorés pour le Dijkstra pathfinding (livraison)
// Augmenté considérablement pour éviter que les ships lointains ne trouvent pas de chemin
constexpr int MAX_NODES_PATHFINDING    = 2500;

/// Nombre max de nœuds explorés pour le Dijkstra greedy
constexpr int MAX_NODES_GREEDY         = 500;

/// Pénalité de coût pour un dropoff allié traversé (hors livraison)
constexpr float DROPOFF_PENALTY        = 3.0f;

/// Profondeur max de la DFS pour le minage local
constexpr int LOCAL_MINING_MAX_DEPTH   = 4;

/// Nombre max de ships en retour simultané par point de dépôt
constexpr int MAX_RETURNING_PER_DROPOFF = 3;

// --- Paramètres différenciés 2p vs 4p ---

/// Énergie minimum restante sur une case pour continuer le minage local (4p)
constexpr int MIN_ENERGY_4P            = 20;
/// Cargo minimum pour valider un chemin de minage local (4p)
constexpr int MIN_CARGO_4P             = 100;
/// Pénalité temporelle pour le minage local et global (4p)
constexpr float TIME_PENALTY_4P        = 3.0f;
/// Pénalité d'ennemi adjacent dans le Dijkstra (4p)
constexpr float ENEMY_ADJACENT_PENALTY_4P = 3.5f;
/// Facteur d'inspiration pour le minage greedy (4p)
constexpr float INSPIRED_FACTOR_4P     = 8.0f;
/// Seuil d'attraction minimal pour le minage global (4p)
constexpr float MIN_ATTRACTION_4P      = 80.0f;

/// Énergie minimum restante sur une case pour continuer le minage local (2p)
constexpr int MIN_ENERGY_2P            = 15;
/// Cargo minimum pour valider un chemin de minage local (2p)
constexpr int MIN_CARGO_2P             = 100;
/// Pénalité temporelle pour le minage local et global (2p)
constexpr float TIME_PENALTY_2P        = 2.5f;
/// Pénalité d'ennemi adjacent dans le Dijkstra (2p)
constexpr float ENEMY_ADJACENT_PENALTY_2P = 0.55f;
/// Facteur d'inspiration pour le minage greedy (2p)
constexpr float INSPIRED_FACTOR_2P     = 1.7f;
/// Seuil d'attraction minimal pour le minage global (2p)
constexpr float MIN_ATTRACTION_2P      = 100.0f;

// ============================================================================
// Structures de données
// ============================================================================

/**
 * @brief État persistant d'un ship entre les tours.
 *
 * Ces informations sont conservées d'un tour à l'autre pour déterminer
 * si un ship est en mode livraison ou a une cible de minage.
 */
struct ShipState {
    bool     is_returning = false;  ///< Le ship est en mode livraison
    Position target{-1, -1};       ///< Cible de minage global persistante (−1 si aucune)
    int      target_age  = 0;      ///< Nombre de tours depuis l'assignation de la cible
};

/// Nombre max de tours qu'un ship garde sa cible de minage global
constexpr int MAX_TARGET_AGE = 20;

/**
 * @brief Candidat pour l'assignation d'un rôle à un ship.
 *
 * Contient le ship concerné, le chemin calculé et le score de ce chemin.
 * Les candidats sont triés par score décroissant pour l'assignation greedy.
 */
struct Candidate {
    EntityId              ship_id;  ///< ID du ship candidat
    std::vector<Position> path;     ///< Chemin planifié (séquence de positions)
    float                 score;    ///< Score du candidat (plus élevé = meilleur)

    /// Comparaison par score décroissant pour le tri
    bool operator<(const Candidate& other) const { return score > other.score; }
};

/**
 * @brief Nœud utilisé dans le Dijkstra pour le pathfinding.
 */
struct DijkstraNode {
    Position pos;      ///< Position sur la carte
    float    cost;     ///< Coût cumulé pour atteindre cette position
    int      turns;    ///< Nombre de tours écoulés

    /// Comparaison par coût croissant pour la priority queue
    bool operator>(const DijkstraNode& other) const { return cost > other.cost; }
};

/**
 * @brief Module de planification stratégique — cœur décisionnel du bot.
 *
 * Assigne un rôle et un chemin à chaque ship allié en utilisant un système
 * de candidats triés par score. Les rôles sont traités par priorité décroissante.
 * Le pathfinding repose sur Dijkstra avec des coûts distordus (halite brûlé,
 * dominance, proximité ennemie).
 */
class PlanningStrategy {
public:
    /**
     * @brief Planifie les mouvements de tous les ships pour le tour courant.
     * @param game     État de jeu courant.
     * @param ann_map  Carte annotée du tour courant.
     * @return Mapping ship_id → direction à jouer.
     */
    std::unordered_map<EntityId, Direction> plan_turn(
        const Game& game, AnnotationMap& ann_map);

private:
    std::unordered_map<EntityId, ShipState> ship_states_; ///< États persistants par ship

    /// @brief Paramètres de jeu dépendant du nombre de joueurs
    int   min_energy_     = MIN_ENERGY_4P;
    int   min_cargo_      = MIN_CARGO_4P;
    float time_penalty_   = TIME_PENALTY_4P;
    float enemy_adj_pen_  = ENEMY_ADJACENT_PENALTY_4P;
    float inspired_factor_= INSPIRED_FACTOR_4P;
    float min_attraction_ = MIN_ATTRACTION_4P;

    /// Seuils adaptatifs (modifiés par adapt_thresholds selon la densité de halite)
    int return_cargo_threshold_ = RETURN_CARGO_THRESHOLD;
    int force_return_cargo_     = FORCE_RETURN_CARGO;
    int last_resort_cargo_      = LAST_RESORT_CARGO;

    /**
     * @brief Configure les paramètres internes selon le nombre de joueurs.
     * @param num_players Nombre de joueurs dans la partie.
     */
    void configure_for_player_count(int num_players);

    /**
     * @brief Adapte les seuils de retour et de minage selon la densité de halite.
     *
     * Sur une carte pauvre (avg ~50), abaisse les seuils pour que les ships
     * retournent plus tôt. Sur une carte riche (avg ~200+), conserve des seuils
     * élevés pour maximiser le cargo par voyage.
     *
     * @param game État de jeu courant.
     */
    void adapt_thresholds(const Game& game);

    /**
     * @brief Nettoie les états des ships qui n'existent plus (coulés).
     * @param game État de jeu courant.
     */
    void cleanup_dead_ships(const Game& game);

    // --- Assignation de rôles ---

    /**
     * @brief Rôle 1 — Retour de fin de partie.
     *
     * Condition : tours restants ≤ distance au dropoff + marge.
     * Tous les ships éligibles reçoivent un chemin vers le dropoff le plus proche.
     */
    std::vector<Candidate> assign_endgame_return(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    /**
     * @brief Rôle 2 — Livraison standard.
     *
     * Condition : cargo ≥ RETURN_CARGO_THRESHOLD ou déjà en mode is_returning.
     * Le ship revient au dropoff le plus proche.
     */
    std::vector<Candidate> assign_returning(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    /**
     * @brief Rôle 3 — Minage local (DFS courte portée).
     *
     * Explore les cases voisines par DFS, simule le minage sur chaque case,
     * et retourne le chemin avec le meilleur score cargo − coûts.
     */
    std::vector<Candidate> assign_local_mining(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    /**
     * @brief Rôle 4 — Minage global (Dijkstra sur le champ d'attraction).
     *
     * Recherche la meilleure case de minage sur la carte en utilisant le
     * champ d'attraction de l'annotation map. MAX_NODES_GLOBAL nœuds explorés.
     */
    std::vector<Candidate> assign_global_mining(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    /**
     * @brief Rôle 5 — Minage greedy (fallback avec bonus attaque).
     *
     * Dijkstra avec scoring simplifié : valeur / (coût + 1).
     * Inclut un bonus si un ennemi vulnérable est sur la case cible.
     */
    std::vector<Candidate> assign_greedy_mining(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    /**
     * @brief Rôle 6 — Dernier recours.
     *
     * Si cargo ≥ LAST_RESORT_CARGO → return. Sinon → rester sur place.
     */
    std::vector<Candidate> assign_last_resort(
        const Game& game, const AnnotationMap& ann_map,
        const std::unordered_set<EntityId>& assigned);

    // --- Pathfinding ---

    /**
     * @brief Calcule le chemin Dijkstra vers le dropoff le plus proche.
     * @param start    Position de départ du ship.
     * @param cargo    Cargo actuel du ship.
     * @param ann_map  Carte annotée.
     * @param game     État de jeu.
     * @param max_nodes Nombre max de nœuds à explorer.
     * @return Paire {chemin, coût}. Chemin vide si aucun dropoff trouvé.
     */
    std::pair<std::vector<Position>, float> dijkstra_to_dropoff(
        const Position& start, int cargo,
        const AnnotationMap& ann_map, const Game& game, int max_nodes);

    /**
     * @brief Calcule le coût Dijkstra pour traverser une case.
     * @param pos         Case cible.
     * @param ann_map     Carte annotée.
     * @param game        État de jeu.
     * @param is_delivery True si le ship est en mode livraison.
     * @return Coût total de traversée (base 1 + pénalités).
     */
    float cell_traversal_cost(
        const Position& pos, const AnnotationMap& ann_map,
        const Game& game, bool is_delivery) const;

    /**
     * @brief Extrait la direction du premier pas d'un chemin.
     * @param from Position actuelle du ship.
     * @param to   Première position du chemin (ou même position si chemin vide).
     * @param game État de jeu (pour get_unsafe_moves).
     * @return Direction à jouer ce tour.
     */
    Direction direction_from_path(
        const Position& from, const Position& to, const Game& game) const;
};

} // namespace hlt
