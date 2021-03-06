#include "AlphaBetaState.h"
#include "AlphaBetaValue.h"
#include "AlphaBetaUnit.h"
#include "AlphaBetaMove.h"
#include "AlphaBetaAction.h"
#include "Util.h"
#include <algorithm>

float getUnitPriority(std::shared_ptr<AlphaBetaUnit> unit, std::shared_ptr<AlphaBetaUnit> target);

AlphaBetaState::AlphaBetaState(AlphaBetaPlayer pplayerMin, AlphaBetaPlayer pplayerMax, long ptime)
    : playerMin(pplayerMin),
    playerMax(pplayerMax),
    time(ptime) { }

// Here we virtually do the planned move. This involes changing the position, health, weapon cooldown and others
// of both player's units
void AlphaBetaState::doMove(AlphaBetaMove * move) {
    // should probably use unit references
    float minTime = INFINITY;
    for (auto action : move->actions) {
        if (action->type == AlphaBetaActionType::ATTACK) {
            // do attack
            action->target->InflictDamage(action->unit->damage);
            action->unit->attack_time = action->time;
        }
        else if (action->type == AlphaBetaActionType::MOVE_BACK || action->type == AlphaBetaActionType::MOVE_FORWARD) {
            // do move
            action->unit->position = action->position;
            action->unit->move_time = action->time;
        }
        action->unit->previous_action = action;
        minTime = std::min(action->time, minTime);
    }
    this->time = minTime;
}

std::tuple<float, float> getPlayersTime(AlphaBetaPlayer min, AlphaBetaPlayer max, float time) {
    float minTime = INFINITY;
    float maxTime = INFINITY;
    for (auto unit : min.units) {
        if (unit->previous_action == nullptr) minTime = std::min(time, 0.f);
        else
            minTime = std::min(time, unit->previous_action->time);
    }
    for (auto unit : max.units) {
        if (unit->previous_action == nullptr) maxTime = std::min(time, 0.f);
        else
            maxTime = std::min(time, unit->previous_action->time);
    }
    return std::make_tuple(minTime, maxTime);
}

bool AlphaBetaState::bothCanMove() {
    std::tuple<float, float> times = getPlayersTime(playerMin, playerMax, time);
    float minTime = std::get<0>(times);
    float maxTime = std::get<1>(times);
    return minTime == maxTime;
}

bool AlphaBetaState::playerToMove() {
    std::tuple<float, float> times = getPlayersTime(playerMin, playerMax, time);
    float minTime = std::get<0>(times);
    float maxTime = std::get<1>(times);
    return maxTime <= minTime;
}

std::vector<AlphaBetaMove *> AlphaBetaState::generateMoves(bool isMax, bool attackClosest, bool attackWeakest, bool attackPriority, bool unitOwnAgent, size_t depth) {
    // should probably be references
    AlphaBetaPlayer player = isMax ? playerMax : playerMin;
    AlphaBetaPlayer ennemy = !isMax ? playerMax : playerMin;
    std::vector<AlphaBetaMove *> possible_moves;
    possible_moves.clear();
    int max_actions = 0;
    std::unordered_map<sc2::Tag, std::vector<AlphaBetaAction *>> actions_per_unit;

    for (auto unit : player.units) {
        std::vector<AlphaBetaAction *> actions;
        int nb_actions = 0;
        // if unit can do something (not cooling down or moving to somewhere)
        // TODO: Move cancelling ? Like if the unit is going somewhere, attack a unit instead
        if (unit->CanAttack(time)) {
            // add attacking closest target in range as actions
            // TODO: Intelligent attacking (like focus fire, weakest ennemy or reuse RangedManager priority)
            // TODO: don't attack dead units

            std::shared_ptr<AlphaBetaUnit> closest_ennemy = nullptr;
            std::shared_ptr<AlphaBetaUnit> weakest_enemy = nullptr;
            std::shared_ptr<AlphaBetaUnit> highest_priority = nullptr;

            float closest_dist = INFINITY;
            float min_health = INFINITY;
            float priority = 0;
            for (auto baddy : ennemy.units) {
                if (baddy->is_dead)
                    continue;
                float dist = Util::Dist(unit->position, baddy->position);
                float health = baddy->hp_current;
                float prio = getUnitPriority(unit, baddy);
                if (dist <= unit->range) {
                    if (dist < closest_dist && attackClosest) {
                        closest_dist = dist;
                        closest_ennemy = baddy;
                    }
                    if (health <= min_health && attackWeakest) {
                        min_health = health;
                        weakest_enemy = baddy;
                    }
                    if (prio > priority && attackPriority) {
                        priority = prio;
                        highest_priority = baddy;
                    }
                }
            }
            if (closest_ennemy != nullptr) {
                AlphaBetaAction * attack;
                attack = new AlphaBetaAction(unit, closest_ennemy, unit->position, 0.f, AlphaBetaActionType::ATTACK, time + unit->cooldown_max);
                actions.push_back(attack);
                ++nb_actions;
            }
            if (weakest_enemy != nullptr) {
                AlphaBetaAction * attack;
                attack = new AlphaBetaAction(unit, weakest_enemy, unit->position, 0.f, AlphaBetaActionType::ATTACK, time + unit->cooldown_max);
                actions.push_back(attack);
                ++nb_actions;
            }
            if (highest_priority != nullptr) {
                AlphaBetaAction * attack;
                attack = new AlphaBetaAction(unit, highest_priority, unit->position, 0.f, AlphaBetaActionType::ATTACK, time + unit->cooldown_max);
                actions.push_back(attack);
                ++nb_actions;
            }
        }

        // Move closer to nearest unit
        // TODO: Incorporate with Attack ?
        if (unit->CanMoveForward(time, ennemy.units)) {
            // add moving closer to targets
            float closest_dist = INFINITY;
            sc2::Point2D closest_point;
            for (auto baddy : ennemy.units) {
                if (Util::Dist(unit->position, baddy->position) > unit->range) {
                    // unit position + attack vector * distance until target in range
                    sc2::Point2D target = unit->position + (Util::Normalized(baddy->position - unit->position) * (Util::Dist(baddy->position, unit->position) - unit->range));
                    if (Util::Dist(target, unit->position) < closest_dist) {
                        closest_dist = Util::Dist(target, unit->position);
                        closest_point = target;
                    }
                }
            }
            AlphaBetaAction * move = new AlphaBetaAction(unit, nullptr, closest_point, closest_dist, AlphaBetaActionType::MOVE_FORWARD, time + (closest_dist / unit->speed));
            actions.push_back(move);
            ++nb_actions;
        }
        // Kite or escape fire
        if (unit->ShouldMoveBack(time, ennemy.units)) {
            AlphaBetaUnit * closest_ennemy = nullptr;
            float closest_dist = INFINITY;
            for (auto baddy : ennemy.units) {
                float dist = Util::Dist(unit->position, baddy->position);
                if (dist < closest_dist) {
                    closest_dist = dist;
                    closest_ennemy = baddy.get();
                }
            }
            if (closest_ennemy != nullptr) {
                AlphaBetaAction * back;
                sc2::Point2D back_position = unit->position - closest_ennemy->position + unit->position;
                closest_dist = Util::Dist(unit->position, back_position);
                back = new AlphaBetaAction(unit, nullptr, back_position, closest_dist, AlphaBetaActionType::MOVE_BACK, time + (closest_dist / unit->speed));
                actions.push_back(back);
                ++nb_actions;
            }
        }

        // TODO: other moves ?   
        actions_per_unit.insert({ unit->actual_unit->tag, actions });
        max_actions = std::max(max_actions, nb_actions);
    }


    if (unitOwnAgent && depth == 0) {
        for (auto unit : player.units) {

            std::vector<AlphaBetaAction *> actions_for_this_unit = actions_per_unit.at(unit->actual_unit->tag);
            if (unit->has_played)
                continue;

            for (auto action : actions_for_this_unit) {
                std::vector<AlphaBetaAction *> actions;
                actions.push_back(action);
                possible_moves.push_back(new AlphaBetaMove(actions));
            }
        }
    }
    else {
        // TODO: Generate better moves, now it's just random
        for (int i = 0; i <= max_actions; ++i) {
            std::vector<AlphaBetaAction *> actions;
            for (auto unit : player.units) {
                std::vector<AlphaBetaAction *> actions_for_this_unit = actions_per_unit.at(unit->actual_unit->tag);
                if (actions_for_this_unit.size()) {
                    int indexRandom = rand() % actions_for_this_unit.size();
                    AlphaBetaAction * action = actions_for_this_unit.at(indexRandom);
                    actions.push_back(action);
                }
            }
            if (actions.size() > 0)
                possible_moves.push_back(new AlphaBetaMove(actions));
        }
    }
    
    return possible_moves;
}

float getUnitPriority(std::shared_ptr<AlphaBetaUnit> unit, std::shared_ptr<AlphaBetaUnit> target) {
    float dps = target->damage;
    if (dps == 0.f)
        dps = 15.f;
    float healthValue = 1 / (target->hp_current + target->shield);
    float distanceValue = 1 / Util::Dist(unit->position, target->position);
    //TODO try to give different weights to each variables
    return 5 + dps * healthValue * distanceValue;
}

AlphaBetaState AlphaBetaState::generateChild() {
    std::vector<std::shared_ptr<AlphaBetaUnit>> minUnits;
    std::vector<std::shared_ptr<AlphaBetaUnit>> maxUnits;

    // so other branches won't influence this one
    for (auto unit : playerMin.units) {
        std::shared_ptr<AlphaBetaUnit> new_unit = std::make_shared<AlphaBetaUnit>(*unit);
        minUnits.push_back(new_unit);
    }
    for (auto unit : playerMax.units) {
        std::shared_ptr<AlphaBetaUnit> new_unit = std::make_shared<AlphaBetaUnit>(*unit);
        maxUnits.push_back(new_unit);
    }

    AlphaBetaPlayer newMin = AlphaBetaPlayer(minUnits, false);
    AlphaBetaPlayer newMax = AlphaBetaPlayer(maxUnits, true);
    return AlphaBetaState(newMin, newMax, time);
}

// TODO: Better decision of which action the unit can do

// TODO: Better evaluation function and end conditions (player dead == -100000) 
AlphaBetaValue AlphaBetaState::eval() {
    float totalPlayerDamage = 0;
    float totalEnemyDamage = 0;
    // We check if at least one unit is still alive. If no unit is alive, we add a huge value.
    bool oneMinIsAlive = false;
    bool oneMaxIsAlive = false;
    for (auto unit : playerMin.units) {
        oneMinIsAlive |= !unit->is_dead;
        totalEnemyDamage += (unit->hp_max - unit->hp_current);
    }
    if (!oneMinIsAlive)
        totalEnemyDamage += 100000;

    for (auto unit : playerMax.units) {
        oneMaxIsAlive |= !unit->is_dead;
        totalPlayerDamage += (unit->hp_max - unit->hp_current);
    }
    if (!oneMaxIsAlive)
        totalPlayerDamage += 100000;

    return AlphaBetaValue(totalEnemyDamage - totalPlayerDamage, nullptr, this);
}