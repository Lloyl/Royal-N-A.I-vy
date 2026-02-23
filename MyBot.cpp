/**
 * @file MyBot.cpp
 * @brief Point d'entrée et boucle de jeu du bot ColonIA (Halite III).
 *
 * Orchestre le pipeline de décision à chaque tour :
 *   1. Mise à jour de l'état de jeu (game.update_frame)
 *   2. Construction de la carte annotée (AnnotationMap)
 *   3. Planification des mouvements (PlanningStrategy)
 *   4. Évaluation de spawn (SpawnPolicy)
 *   5. Évaluation de dropoff (DropoffPolicy)
 *   6. Résolution des collisions (CollisionPolicy)
 *   7. Génération et envoi des commandes
 *
 * Logging structuré avec timing de chaque étape, état du jeu par tour,
 * et détail des commandes émises.
 */

// --- Standard library ---
#include <chrono>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <optional>

// --- Halite framework ---
#include "game.hpp"
#include "constants.hpp"
#include "log.hpp"
#include "command.hpp"

// --- HaliteAI modules ---
#include "annotation_map.hpp"
#include "planning_strategy.hpp"
#include "spawn_policy.hpp"
#include "dropoff_policy.hpp"
#include "collision_policy.hpp"

using namespace hlt;

/// Calcule le halite total initial sur la carte (appelé une seule fois)
static int compute_initial_halite(const Game& game) {
    int total = 0;
    for (int y = 0; y < game.game_map->height; ++y) {
        for (int x = 0; x < game.game_map->width; ++x) {
            total += game.game_map->cells[y][x].halite;
        }
    }
    return total;
}

/// Convertit un std::chrono::duration en millisecondes (double)
template<typename Duration>
static double to_ms(Duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

int main(int /* argc */, char* /* argv */[]) {

    Game game;
    game.ready("ColonIA");

    // Modules de décision persistants entre les tours
    AnnotationMap    annotation_map;
    PlanningStrategy planning_strategy;
    SpawnPolicy      spawn_policy;
    DropoffPolicy    dropoff_policy;
    CollisionPolicy  collision_policy;

    // Halite initial — calculé au premier tour pour la politique de spawn
    int initial_halite = 0;
    bool first_turn = true;

    // Décision de dropoff persistante entre les tours
    std::optional<DropoffDecision> active_dropoff;

    // ========================================================================
    // Boucle de jeu principale
    // ========================================================================
    for (;;) {
        game.update_frame();

        auto turn_start = std::chrono::high_resolution_clock::now();

        // Calculer le halite initial au premier tour uniquement
        if (first_turn) {
            initial_halite = compute_initial_halite(game);
            first_turn = false;
        }

        // --- Logging d'état ---
        log::log("--- Turn " + std::to_string(game.turn_number) + " ---");
        log::log("Ships: " + std::to_string(game.me->ships.size()) +
                 " | Halite: " + std::to_string(game.me->halite) +
                 " | Dropoffs: " + std::to_string(game.me->dropoffs.size()));

        // ====================================================================
        // Étape 1 : Construction de la carte annotée
        // ====================================================================
        auto t0 = std::chrono::high_resolution_clock::now();
        annotation_map.build(game);
        auto t1 = std::chrono::high_resolution_clock::now();
        log::log("AnnotationMap::build: " + std::to_string(to_ms(t1 - t0)) + " ms");

        // ====================================================================
        // Étape 2 : Planification des mouvements
        // ====================================================================
        auto t2 = std::chrono::high_resolution_clock::now();
        auto moves = planning_strategy.plan_turn(game, annotation_map);
        auto t3 = std::chrono::high_resolution_clock::now();
        log::log("PlanningStrategy::plan_turn: " + std::to_string(to_ms(t3 - t2)) + " ms");

        // ====================================================================
        // Étape 3 : Évaluation de dropoff (avant spawn pour réserver du halite)
        // ====================================================================

        // Vérifier si le constructeur actif est toujours en vie et si la cible est valide
        if (active_dropoff.has_value()) {
            bool valid = true;
            // 1. Builder exists?
            if (game.me->ships.find(active_dropoff->builder_id) == game.me->ships.end()) {
                valid = false;
                log::log("Dropoff builder lost — resetting");
            }
            // 2. Target still empty?
            else {
                Position target = active_dropoff->target;
                if (game.game_map->at(target)->has_structure()) {
                    valid = false;
                    log::log("Dropoff target occupied by structure — resetting");
                }
                // Check specifically against shipyard position just in case
                if (target == game.me->shipyard->position) {
                    valid = false;
                    log::log("Dropoff target is shipyard — resetting");
                }
                // Check against existing dropoffs (redundant with has_structure but safer)
                for (const auto& [did, d] : game.me->dropoffs) {
                    if (d->position == target) {
                        valid = false;
                        log::log("Dropoff target is existing dropoff — resetting");
                        break;
                    }
                }
            }

            if (!valid) {
                 active_dropoff = std::nullopt;
            }
        }

        // Évaluer un nouveau dropoff seulement si aucun n'est en cours
        auto dropoff_decision = active_dropoff;
        if (!dropoff_decision.has_value()) {
            dropoff_decision = dropoff_policy.evaluate(game, annotation_map);
            if (dropoff_decision.has_value()) {
                active_dropoff = dropoff_decision;
                log::log("New dropoff target at (" +
                         std::to_string(dropoff_decision->target.x) + "," +
                         std::to_string(dropoff_decision->target.y) +
                         ") builder=" + std::to_string(dropoff_decision->builder_id));
            }
        }

        // Naviguer le constructeur vers la cible du dropoff
        if (dropoff_decision.has_value()) {
            EntityId builder_id = dropoff_decision->builder_id;
            auto builder_it = game.me->ships.find(builder_id);
            if (builder_it != game.me->ships.end()) {
                const auto& builder = builder_it->second;
                if (builder->position != dropoff_decision->target) {
                    auto nav = game.game_map->get_unsafe_moves(
                        builder->position, dropoff_decision->target);
                    if (!nav.empty()) {
                        moves[builder_id] = nav[0];
                    }
                }
            }
        }

        // ====================================================================
        // Étape 4 : Évaluation de spawn
        // ====================================================================
        bool do_spawn = spawn_policy.should_spawn(game, annotation_map, initial_halite);

        // Économiser du halite pour la construction d'un dropoff sur cartes ≥ 48
        bool saving_for_dropoff = (game.game_map->width >= 48 &&
            game.me->dropoffs.empty() &&
            static_cast<int>(game.me->ships.size()) >= 8 &&
            game.turn_number < constants::MAX_TURNS * 3 / 4);

        if (saving_for_dropoff && do_spawn && game.me->halite < 3000) {
            do_spawn = false;
            log::log("Spawn deferred: saving halite for dropoff");
        }

        // Si un dropoff est en cours de construction, priorité au dropoff
        if (dropoff_decision.has_value() && do_spawn) {
            do_spawn = false;
            log::log("Spawn cancelled: dropoff construction in progress");
        }

        // ====================================================================
        // Étape 5 : Résolution des collisions
        // ====================================================================
        auto t4 = std::chrono::high_resolution_clock::now();
        collision_policy.resolve_collisions(game, moves, do_spawn);
        auto t5 = std::chrono::high_resolution_clock::now();
        log::log("CollisionPolicy::resolve: " + std::to_string(to_ms(t5 - t4)) + " ms");

        // ====================================================================
        // Étape 6 : Génération des commandes
        // ====================================================================
        std::vector<Command> command_queue;

        // Commandes de mouvement pour chaque ship
        for (const auto& [ship_id, ship] : game.me->ships) {
            Direction dir = Direction::STILL;

            // Vérifier si ce ship doit construire le dropoff
            if (dropoff_decision.has_value() &&
                dropoff_decision->builder_id == ship_id &&
                ship->position == dropoff_decision->target)
            {
                // Vérifier le coût effectif avant de construire
                int cell_h = game.game_map->at(ship->position)->halite;
                int eff_cost = std::max(0, constants::DROPOFF_COST
                             - ship->halite - cell_h);
                if (game.me->halite >= eff_cost) {
                    command_queue.push_back(ship->make_dropoff());
                    active_dropoff = std::nullopt;
                    log::log("Ship " + std::to_string(ship_id) + " -> BUILD DROPOFF");
                    continue;
                }
            }

            // Direction planifiée (ou STILL par défaut)
            auto move_it = moves.find(ship_id);
            if (move_it != moves.end()) {
                dir = move_it->second;
            }

            command_queue.push_back(command::move(ship_id, dir));
            log::log("Ship " + std::to_string(ship_id) + " -> " +
                     std::string(1, static_cast<char>(dir)));
        }

        // Commande de spawn
        if (do_spawn) {
            command_queue.push_back(command::spawn_ship());
            log::log("SPAWN new ship");
        } else {
            // Log de la raison de non-spawn (tour avancé ou halite basse)
            if (game.me->halite < constants::SHIP_COST) {
                log::log("No spawn: insufficient halite");
            } else if (game.turn_number >= constants::MAX_TURNS - 100) {
                log::log("No spawn: endgame");
            }
        }

        // --- Timing total du tour ---
        auto turn_end = std::chrono::high_resolution_clock::now();
        log::log("Total turn time: " + std::to_string(to_ms(turn_end - turn_start)) + " ms");

        // Soumettre les commandes
        if (!game.end_turn(command_queue)) {
            break;
        }
    }

    return 0;
}