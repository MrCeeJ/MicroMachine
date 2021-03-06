#pragma once
#include "AlphaBetaConsideringDurations.h"
#include "AlphaBetaTimeOut.h"

AlphaBetaConsideringDurations::AlphaBetaConsideringDurations(std::chrono::milliseconds time, size_t depth, bool pUnitOwnAgent, bool pClosestEnemy, bool pWeakestEnemy, bool pHighestPriority)
    : time_limit(time),
    start{},
    depth_limit(depth),
    unitOwnAgent(pUnitOwnAgent),
    closestEnemy(pClosestEnemy),
    weakestEnemy(pWeakestEnemy),
    highestPriority(pHighestPriority)
{
    nodes_evaluated = 0;
}


AlphaBetaValue AlphaBetaConsideringDurations::doSearch(std::vector<std::shared_ptr<AlphaBetaUnit>> units, std::vector<std::shared_ptr<AlphaBetaUnit>> targets, CCBot * bot)
{
    start = std::chrono::high_resolution_clock::now();

    AlphaBetaPlayer min = AlphaBetaPlayer(targets, false);

    AlphaBetaPlayer max = AlphaBetaPlayer(units, false);

    AlphaBetaState state = AlphaBetaState(min, max, 0);
    AlphaBetaValue alpha(-10000, nullptr, nullptr), beta(10000, nullptr, nullptr);
    return alphaBeta(state, depth_limit, nullptr, alpha, beta);
}

bool isTerminal(AlphaBetaState state, size_t depth) {
    if (depth == 0) return true;
    return state.playerMin.units.size() == 0 || state.playerMax.units.size() == 0;
}

AlphaBetaValue AlphaBetaConsideringDurations::alphaBeta(AlphaBetaState state, size_t depth, AlphaBetaMove * m0, AlphaBetaValue alpha, AlphaBetaValue beta) {
    
    auto end = std::chrono::high_resolution_clock::now();
    auto timeElapse = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (timeElapse.count() > time_limit.count())
    {
        throw AlphaBetaTimeOut();
    }
    if (isTerminal(state, depth)) return state.eval();

    // MAX == true
    bool toMove = state.playerToMove();

    std::vector<AlphaBetaMove *> moves = state.generateMoves(toMove, closestEnemy, weakestEnemy, highestPriority, unitOwnAgent,  depth_limit - depth);
    for (auto m : moves) {
        AlphaBetaValue val;
        if (state.bothCanMove() && m0 == nullptr && depth != 1)
            val = alphaBeta(state, depth - 1, m, alpha, beta);
        AlphaBetaState child = state.generateChild();
        if (m0 != nullptr) {
            child.doMove(m0);
        }
        child.doMove(m);

        val = alphaBeta(child, depth - 1, nullptr, alpha, beta);

        if (toMove && (val.score > alpha.score)) {
            alpha = AlphaBetaValue(val.score, m, &child);
        }
        if (!toMove && (val.score < beta.score)) {
            beta = AlphaBetaValue(val.score, m, &child);
        }
        if (alpha.score > beta.score) break;
    }
    // TODO: More stats ?
    ++nodes_evaluated;
    // TODO: free-up memory it's getting huge in here
    return toMove ? alpha : beta;
}
