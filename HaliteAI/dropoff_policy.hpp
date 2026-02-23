#pragma once

/**
 * @file dropoff_policy.hpp
 * @brief Politique de construction de dropoffs — évalue et sélectionne un emplacement.
 *
 * Ce module analyse la carte pour identifier la meilleure position de construction
 * d'un nouveau dropoff. Les critères incluent la distance aux dropoffs existants,
 * la dominance locale, le nombre de ships alliés à proximité et la densité de
 * halite dans le voisinage.
 *
 * Module réutilisable : ne dépend que de hlt::Game et AnnotationMap.
 */

#include <optional>

#include "game.hpp"
#include "types.hpp"
#include "position.hpp"
#include "constants.hpp"

#include "annotation_map.hpp"

namespace hlt {

/// Distance minimum entre les dropoffs pour une carte 32×32
constexpr int DROPOFF_MIN_DIST_32 = 8;
/// Distance minimum entre les dropoffs pour une carte 48×48
constexpr int DROPOFF_MIN_DIST_48 = 10;
/// Distance minimum entre les dropoffs pour une carte 64×64
constexpr int DROPOFF_MIN_DIST_64 = 12;

/// Seuil de dominance pour les grandes cartes (largeur ≥ 56)
constexpr int DROPOFF_DOMINANCE_THRESHOLD = -5;

/// Nombre min de ships alliés dans un rayon de 10 pour valider un dropoff
constexpr int DROPOFF_MIN_NEARBY_SHIPS = 3;
/// Rayon de recherche pour les ships alliés proches
constexpr int DROPOFF_SHIP_RADIUS = 10;

/// Halite minimum dans un rayon de 7 autour du dropoff candidat
constexpr int DROPOFF_MIN_HALITE_NEARBY = 2000;
/// Rayon de recherche pour le halite autour du dropoff
constexpr int DROPOFF_HALITE_RADIUS = 7;

/**
 * @brief Résultat de l'évaluation de la politique de dropoff.
 *
 * Contient la position recommandée et l'ID du ship le plus proche
 * chargé de la construction.
 */
struct DropoffDecision {
    Position target;      ///< Position du dropoff à construire
    EntityId builder_id;  ///< Ship le plus proche chargé de la construction
};

/**
 * @brief Politique de construction de dropoffs.
 *
 * Évalue toutes les cases de la carte selon un ensemble de critères
 * (distance, dominance, présence alliée, densité de halite) et
 * retourne la meilleure candidate, s'il en existe une.
 */
class DropoffPolicy {
public:
    /**
     * @brief Évalue si un dropoff doit être construit et où.
     * @param game    État de jeu courant.
     * @param ann_map Carte annotée du tour courant.
     * @return DropoffDecision si un bon emplacement est identifié, sinon std::nullopt.
     */
    std::optional<DropoffDecision> evaluate(
        const Game& game, const AnnotationMap& ann_map) const;

private:
    /**
     * @brief Retourne la distance minimum entre dropoffs selon la taille de carte.
     * @param map_width Largeur de la carte.
     * @return Distance minimum inter-dropoff.
     */
    int min_dropoff_distance(int map_width) const;

    /**
     * @brief Calcule le halite total dans un rayon autour d'une position.
     * @param game   État de jeu.
     * @param center Position centrale.
     * @param radius Rayon de recherche.
     * @return Somme du halite dans le rayon.
     */
    int halite_in_radius(const Game& game, const Position& center, int radius) const;

    /**
     * @brief Compte les ships alliés dans un rayon autour d'une position.
     * @param game   État de jeu.
     * @param center Position centrale.
     * @param radius Rayon de recherche.
     * @return Nombre de ships alliés dans le rayon.
     */
    int allied_ships_in_radius(const Game& game, const Position& center, int radius) const;
};

} // namespace hlt
