// Frozen reference search for the tests: plain minimax (negamax form), written separately from
// the engine's search (src/minimax.cpp). The tests check that every Minimax engine variant
// returns exactly this function's score and move at a fixed depth. Do not extend or "improve"
// it: its value is that it is simple and never changes.
#pragma once

#include <cstddef>
#include <tuple>

#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/rng.h"

namespace rs::reference {

// Score of pos for the side to move, searching `depth` more plies.
inline int negamax(Position& pos, int depth, int ply) {
    MoveList moves{};
    generateLegalMoves(pos, moves);
    if (moves.empty()) return pos.inCheck() ? -(Engine::MATE_CPAWNS - ply) : 0;
    if (depth == 0) return Engine::eval_material(pos);

    int best = -Engine::INF_CPAWNS;
    for (Move move : moves) {
        pos.makeMove(move);
        const int score = -negamax(pos, depth - 1, ply + 1);
        pos.unmakeMove();
        if (score > best) best = score;
    }
    return best;
}

// Best move and its score. Tied moves are collected in move-generation order and one is picked
// with `rng`, the same way the engines do, so with the same seed the chosen moves must match.
inline std::tuple<Move, int> minimax(const Position& root, int depth, Rng& rng) {
    Position pos = root;
    MoveList moves{};
    generateLegalMoves(pos, moves);
    if (moves.empty()) return {Move{}, pos.inCheck() ? -Engine::MATE_CPAWNS : 0};

    MoveList best_moves;
    int best = -Engine::INF_CPAWNS;
    for (Move move : moves) {
        pos.makeMove(move);
        const int score = -negamax(pos, depth - 1, 1);
        pos.unmakeMove();
        if (score > best) {
            best = score;
            best_moves.clear();
            best_moves.push(move);
        } else if (score == best) {
            best_moves.push(move);
        }
    }
    return {best_moves[static_cast<std::size_t>(rng.below(best_moves.size()))], best};
}

}  // namespace rs::reference
