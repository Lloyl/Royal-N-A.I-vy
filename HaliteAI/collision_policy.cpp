/**
 * @file collision_policy.cpp
 * @brief Implémentation de la résolution anti-collision.
 *
 * Safety net qui corrige les mouvements planifiés pour éviter les
 * auto-collisions entre ships alliés. Deux types de conflits sont
 * détectés et résolus :
 *   - Conflits de destination (2+ ships → même case)
 *   - Swaps (A→B et B→A simultanés)
 *
 * Le processus est itéré jusqu'à ce qu'aucun conflit ne subsiste.
 * Exception end-game : les collisions sur dropoffs alliés sont ignorées.
 */

#include "collision_policy.hpp"

#include <vector>
#include <unordered_set>

namespace hlt {

void CollisionPolicy::resolve_collisions(
    const Game& game,
    std::unordered_map<EntityId, Direction>& moves,
    bool spawning) const
{
    int turns_remaining = constants::MAX_TURNS - game.turn_number;
    bool endgame = (turns_remaining < ENDGAME_COLLISION_TURNS);

    auto norm = [&game](const Position& p) -> Position {
        return game.game_map->normalize(p);
    };

    // Si on spawn, le shipyard doit être protégé (destination interdite pour les ships existants)
    Position shipyard_pos = game.me->shipyard->position;

    /*
     * Résolution itérative : on refait des passes tant qu'il y a des conflits.
     * Chaque passe construit la carte des destinations et détecte les doublons.
     * La boucle converge car chaque correction (ship → STILL) réduit un conflit
     * sans en créer de nouveau (la case d'origine est libérée ou déjà occupée).
     */
    bool changed = true;
    while (changed) {
        changed = false;

        // Construire la carte destination → ship_id
        std::unordered_map<Position, std::vector<EntityId>> dest_map;

        for (const auto& [ship_id, dir] : moves) {
            auto ship_it = game.me->ships.find(ship_id);
            if (ship_it == game.me->ships.end()) continue;

            Position dest = norm(ship_it->second->position.directional_offset(dir));
            dest_map[dest].push_back(ship_id);
        }

        // Si on spawn, ajouter un "ship fantôme" sur le shipyard pour forcer les collisions
        if (spawning) {
            // Un spawn est équivalent à un ship qui arrive sur le shipyard
            // Si quelqu'un d'autre veut aller sur le shipyard, c'est un conflit
            if (dest_map.count(shipyard_pos)) {
                // Pour la logique de résolution, on traite le spawn comme un ship prioritaire
                // Tous les autres ships visant le shipyard doivent être redirigés
                // On simule cela en ne faisant rien ici, mais en traitant le cas ci-dessous
            }
        }

        // Détecter et résoudre les conflits de destination
        for (auto& [dest, ship_ids] : dest_map) {
            
            // Check spécial spawn : si la destination est le shipyard et qu'on spawn,
            // c'est un conflit même s'il n'y a qu'un seul ship qui veut y aller.
            bool clash_with_spawn = (spawning && dest == shipyard_pos);

            if (ship_ids.size() <= 1 && !clash_with_spawn) continue;

            // Exception end-game : autoriser les collisions sur les dropoffs alliés
            // MAIS JAMAIS sur le shipyard si on est en train de spawner dessus
            if (endgame && !clash_with_spawn && is_allied_dropoff(game, dest)) continue;

            /*
             * Résolution : si un ship est déjà STILL à cette position (il y
             * « réside »), il garde la case et tous les autres doivent changer.
             * Sinon, le ship avec le plus de cargo garde sa destination.
             * Les ships évincés tentent une direction alternative AVANT de
             * se rabattre sur STILL (pour éviter les blocages en chaîne).
             */
            EntityId best_ship = -1;

            // Si conflit avec spawn, AUCUN ship ne peut aller sur le shipyard
            // Sauf s'il quitte le shipyard... Attends.
            // Si un ship est SUR le shipyard et va AILLEURS, sa dest n'est pas le shipyard.
            // Donc ici on ne traite que les dest == shipyard.
            // Donc si clash_with_spawn est vrai, TOUS les ships dans ship_ids doivent dégager.
            if (clash_with_spawn) {
                best_ship = -1; // Personne ne gagne
            } else {
                // Priorité aux ships résidents (STILL à leur position = destination)
                for (auto sid : ship_ids) {
                    if (moves[sid] == Direction::STILL) {
                        best_ship = sid;
                        break;
                    }
                }

                // Si aucun résident, choisir le ship avec le plus de cargo
                if (best_ship == -1) {
                    int best_cargo = -1;
                    for (auto sid : ship_ids) {
                        auto it = game.me->ships.find(sid);
                        if (it != game.me->ships.end() && it->second->halite > best_cargo) {
                            best_cargo = it->second->halite;
                            best_ship = sid;
                        }
                    }
                }
            }

            for (auto sid : ship_ids) {
                if (sid == best_ship) continue;
                // Si move déjà STILL et qu'on est au shipyard (cas résident), 
                // on Doit bouger si spawn. Mais si on est STILL, on est le résident
                // et on a été targeté par le conflit spawn.
                // Si on est STILL au shipyard et spawn=true, on doit bouger AILLEURS
                // ou le spawn doit être annulé.
                // Notre logique actuelle ne peut pas annuler le spawn ici (c'est trop tard
                // dans le pipeline ou l'appelant ne gère pas le retour).
                // => On force le mouvement, mais s'il n'y a pas de mouvement safe, on est coincé.
                // => Le SpawnPolicy vérifie 'shipyard occupied', donc si on est STILL dessus,
                // le SpawnPolicy a retourné false, donc spawning=false.
                // Donc ce cas (STILL sur shipyard + spawning=true) est IMPOSSIBLE si SpawnPolicy marche bien.
                // Le seul cas possible est un ship venant d'AILLEURS vers le shipyard.
                
                if (moves[sid] == Direction::STILL && !clash_with_spawn) continue;

                auto ship_it = game.me->ships.find(sid);
                if (ship_it == game.me->ships.end()) continue;

                Position ship_pos = ship_it->second->position;
                bool redirected = false;

                // Essayer les 4 directions cardinales comme alternative
                for (auto alt_d : ALL_CARDINALS) {
                    if (alt_d == moves[sid]) continue;
                    Position alt_dest = norm(ship_pos.directional_offset(alt_d));
                    
                    // Si on est le résident du shipyard (cas impossible cf plus haut mais sécurisé),
                    // on ne doit pas rester sur place.
                    if (clash_with_spawn && alt_d == Direction::STILL) continue;
                    
                    auto dm_it = dest_map.find(alt_dest);
                    bool is_free = (dm_it == dest_map.end() || dm_it->second.empty());
                    
                    // Si on spawn, le shipyard est interdit comme alternative aussi
                    if (spawning && alt_dest == shipyard_pos) is_free = false;

                    if (is_free) {
                        moves[sid] = alt_d;
                        dest_map[alt_dest].push_back(sid);
                        redirected = true;
                        changed = true;
                        break;
                    }
                }

                if (!redirected) {
                    moves[sid] = Direction::STILL;
                    // Si on est retombé sur STILL et que la position est le shipyard,
                    // et qu'on spawn, c'est la cata (collision spawn).
                    // Mais encore une fois, si on est STILL au shipyard, on l'était au début du tour,
                    // donc SpawnPolicy a bloqué.
                    // Donc le seul risque est un ship VOISIN qui veut aller au shipyard,
                    // ne trouve pas d'alternative, et fait STILL sur sa case voisine.
                    // C'est OK, il ne va pas au shipyard.
                    changed = true;
                }
            }
        }


        // Détecter les swaps (A→B et B→A)
        std::unordered_set<EntityId> swapped;
        for (const auto& [id_a, dir_a] : moves) {
            if (swapped.count(id_a)) continue;
            if (dir_a == Direction::STILL) continue;

            auto ship_a_it = game.me->ships.find(id_a);
            if (ship_a_it == game.me->ships.end()) continue;

            Position pos_a = ship_a_it->second->position;
            Position dest_a = norm(pos_a.directional_offset(dir_a));

            for (const auto& [id_b, dir_b] : moves) {
                if (id_a == id_b) continue;
                if (swapped.count(id_b)) continue;
                if (dir_b == Direction::STILL) continue;

                auto ship_b_it = game.me->ships.find(id_b);
                if (ship_b_it == game.me->ships.end()) continue;

                Position pos_b = ship_b_it->second->position;
                Position dest_b = norm(pos_b.directional_offset(dir_b));

                // Swap détecté : A veut aller sur B, B veut aller sur A
                if (dest_a == norm(pos_b) && dest_b == norm(pos_a)) {
                    // Exception end-game sur dropoff
                    if (endgame && (is_allied_dropoff(game, dest_a) ||
                                    is_allied_dropoff(game, dest_b))) {
                        continue;
                    }

                    // Celui avec le moins de cargo reste STILL
                    if (ship_a_it->second->halite <= ship_b_it->second->halite) {
                        if (moves[id_a] != Direction::STILL) {
                            moves[id_a] = Direction::STILL;
                            changed = true;
                        }
                    } else {
                        if (moves[id_b] != Direction::STILL) {
                            moves[id_b] = Direction::STILL;
                            changed = true;
                        }
                    }
                    swapped.insert(id_a);
                    swapped.insert(id_b);
                }
            }
        }
    }
}

bool CollisionPolicy::is_allied_dropoff(const Game& game, const Position& pos) const {
    Position n = game.game_map->normalize(pos);
    const MapCell* cell = game.game_map->at(n);

    if (!cell->has_structure()) return false;
    return (cell->structure->owner == game.my_id);
}

} // namespace hlt
