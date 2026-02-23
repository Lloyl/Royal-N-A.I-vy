#pragma once

/**
 * @file annotation_map.hpp
 * @brief Carte annotée pré-calculée chaque tour pour guider les décisions stratégiques.
 *
 * Ce module construit une couche d'annotations au-dessus de la GameMap du moteur.
 * Il calcule un champ d'attraction (halite blurré), des scores de dominance
 * allié/ennemi, des distances BFS multi-source, l'inspiration, et la portée
 * ennemie. Ces données sont consommées par les modules de planification,
 * de spawn, de dropoff et de collision.
 */

#include <cstdint>
#include <vector>
#include <queue>

#include "game.hpp"
#include "game_map.hpp"
#include "player.hpp"
#include "position.hpp"
#include "constants.hpp"

namespace hlt {

/// Rayon utilisé pour le calcul de dominance (alliés − ennemis)
constexpr int DOMINANCE_RADIUS = 5;

/// Seuil d'attraction minimal pour qu'une case soit considérée intéressante
constexpr float ATTRACTION_THRESHOLD = 100.0f;

/// Facteur de blur exponentiel par défaut (4 joueurs)
constexpr float DEFAULT_BLUR_FACTOR = 0.75f;

/// Facteur de blur exponentiel pour les parties à 2 joueurs
constexpr float BLUR_FACTOR_2P = 0.623f;

/**
 * @brief Informations annotées pour une case de la carte.
 *
 * Chaque cellule de la grille possède ces métadonnées, recalculées
 * intégralement à chaque tour par AnnotationMap::build().
 */
struct CellInfo {
    float attraction       = 0.0f;   ///< Halite blurré (blur exponentiel + seuil)
    int   dominance_score  = 0;      ///< Alliés − Ennemis dans un rayon DOMINANCE_RADIUS
    bool  is_inspired      = false;  ///< ≥ INSPIRATION_SHIP_COUNT ennemis dans INSPIRATION_RADIUS
    bool  has_enemy_ship   = false;  ///< Un ennemi occupe cette case
    bool  enemy_reachable  = false;  ///< Un ennemi peut atteindre cette case au prochain tour
    int   dist_to_ally     = 9999;   ///< Distance Manhattan au plus proche allié
    int   dist_to_enemy    = 9999;   ///< Distance Manhattan au plus proche ennemi
    int   dist_to_dropoff  = 9999;   ///< Distance Manhattan au plus proche dropoff/shipyard allié
    bool  marked_for_mining = false; ///< Case réservée par un ship pour minage futur
    uint64_t occupation_mask = 0;    ///< Bitmask des tours où la case sera occupée (64 tours max)
};

/**
 * @brief Carte annotée construite chaque tour au-dessus de la GameMap.
 *
 * Fournit des informations pré-calculées (attraction, dominance, distances,
 * inspiration, portée ennemie) utilisées par tous les modules décisionnels.
 * Conception réutilisable : ne dépend que de hlt::Game et peut être instanciée
 * indépendamment pour des tests unitaires.
 */
class AnnotationMap {
public:
    /**
     * @brief Construit l'intégralité de la carte annotée pour le tour courant.
     * @param game  Référence vers l'état de jeu courant (carte, joueurs, tour).
     *
     * Appelle séquentiellement les sous-méthodes de construction :
     * attraction → dominance → distances → inspiration → portée ennemie.
     */
    void build(Game& game);

    /**
     * @brief Marque un chemin dans la carte annotée (occupation + minage).
     * @param path       Vecteur de positions constituant le chemin.
     * @param start_turn Tour de départ relatif pour le bitmask d'occupation.
     * @param mark_mining Si true, la dernière position est marquée pour minage.
     */
    void mark_path(const std::vector<Position>& path, int start_turn, bool mark_mining);

    /**
     * @brief Accède aux informations annotées d'une position (normalisée).
     * @param pos Position sur la carte (sera normalisée en interne).
     * @return Référence constante vers le CellInfo correspondant.
     */
    const CellInfo& at(const Position& pos) const;

    /**
     * @brief Accède aux informations annotées d'une position (version mutable).
     * @param pos Position sur la carte (sera normalisée en interne).
     * @return Référence mutable vers le CellInfo correspondant.
     */
    CellInfo& at_mut(const Position& pos);

    /// @brief Largeur de la carte
    int width() const { return width_; }

    /// @brief Hauteur de la carte
    int height() const { return height_; }

    /// @brief Nombre de joueurs dans la partie
    int num_players() const { return num_players_; }

    /**
     * @brief Réinitialise toutes les annotations pour un nouveau tour.
     *
     * Appelée automatiquement au début de build(). Remet tous les
     * CellInfo à leurs valeurs par défaut.
     */
    void reset();

private:
    int width_  = 0;
    int height_ = 0;
    int num_players_ = 0;
    std::vector<std::vector<CellInfo>> grid_;  ///< Grille 2D de CellInfo [y][x]

    GameMap* game_map_ = nullptr;          ///< Pointeur vers la carte du moteur (non possédé)
    PlayerId my_id_ = -1;                 ///< ID du joueur allié

    /// @brief Normalise une position sur la carte toroïdale.
    Position normalize(const Position& pos) const;

    /**
     * @brief Construit le champ d'attraction par blur exponentiel + seuil.
     *
     * Deux passes (horizontale puis verticale) pour un blur séparable O(W×H).
     * Le facteur de blur dépend du nombre de joueurs (4p vs 2p).
     */
    void build_attraction_field();

    /**
     * @brief Calcule le score de dominance pour chaque case.
     *
     * Pour chaque case, compte les alliés et ennemis dans un rayon
     * DOMINANCE_RADIUS et fait la différence. Score > 0 = zone alliée.
     */
    void calculate_dominance(Game& game);

    /**
     * @brief Calcule les distances BFS multi-source vers alliés, ennemis et dropoffs.
     *
     * Trois BFS multi-source en O(W×H) pour remplir dist_to_ally,
     * dist_to_enemy et dist_to_dropoff dans chaque CellInfo.
     */
    void calculate_distances(Game& game);

    /**
     * @brief Détermine quelles cases sont inspirées.
     *
     * Une case est inspirée si au moins INSPIRATION_SHIP_COUNT ships
     * ennemis se trouvent dans un rayon INSPIRATION_RADIUS.
     */
    void calculate_inspiration(Game& game);

    /**
     * @brief Marque les cases atteignables par un ennemi au tour suivant.
     *
     * Chaque ship ennemi peut se déplacer d'une case dans chaque direction
     * cardinale ou rester sur place : ces 5 positions sont marquées.
     */
    void calculate_enemy_reach(Game& game);
};

} // namespace hlt
