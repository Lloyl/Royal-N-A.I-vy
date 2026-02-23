#pragma once

/**
 * @file spawn_policy.hpp
 * @brief Politique de spawn — décide si un nouveau ship doit être créé.
 *
 * Ce module encapsule les règles de décision pour le spawn de ships.
 * Les conditions évaluées sont : halite disponible, occupation du shipyard,
 * tour courant, halite restant sur la carte, et nombre max de ships selon
 * la taille de la carte.
 *
 * Module réutilisable : aucune dépendance interne hormis le framework hlt
 * et l'AnnotationMap. Peut être instancié indépendamment pour des tests.
 */

#include "game.hpp"
#include "constants.hpp"

#include "annotation_map.hpp"

namespace hlt {

/// Marge de tours avant la fin au-delà de laquelle on ne spawn plus
constexpr int SPAWN_TURN_MARGIN = 100;

/// Ratio du halite initial sous lequel on arrête de spawner
constexpr float MIN_HALITE_RATIO = 0.15f;

/// Cap de ships pour une carte 32×32
constexpr int SHIP_CAP_32 = 10;
/// Cap de ships pour une carte 48×48
constexpr int SHIP_CAP_48 = 18;
/// Cap de ships pour une carte 64×64
constexpr int SHIP_CAP_64 = 25;

/**
 * @brief Politique de spawn pour les ships alliés.
 *
 * Évalue un ensemble de conditions simples et efficaces pour
 * déterminer si un nouveau ship doit être créé ce tour-ci.
 * Les conditions sont volontairement conservatrices pour
 * maximiser le retour sur investissement de chaque ship.
 */
class SpawnPolicy {
public:
    /**
     * @brief Détermine si un ship doit être spawné ce tour.
     * @param game           État de jeu courant.
     * @param ann_map        Carte annotée (pour dist_to_dropoff, etc.).
     * @param initial_halite Halite total au début de la partie.
     * @return True si toutes les conditions de spawn sont réunies.
     */
    bool should_spawn(const Game& game, const AnnotationMap& ann_map,
                      int initial_halite) const;

private:
    /**
     * @brief Retourne le nombre max de ships autorisé selon la taille de carte.
     * @param map_width Largeur de la carte.
     * @return Cap de ships adapté.
     */
    int ship_cap(int map_width) const;

    /**
     * @brief Calcule le halite total restant sur la carte.
     * @param game État de jeu courant.
     * @return Somme du halite de toutes les cases.
     */
    int total_map_halite(const Game& game) const;
};

} // namespace hlt
