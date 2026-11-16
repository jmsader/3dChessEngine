// Frozen reference search for the tests: plain minimax (negamax form), written separately from
// the engine's search (src/minimax.cpp). The tests check that every Minimax engine variant
// returns this search's score at a fixed depth and plays one of its best moves. Do not
// "improve" it: its value is that it is simple and never changes.
#pragma once

#include <cstddef>
#include <tuple>

#include "raumschach/engine.h"
#include "raumschach/movegen.h"

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

// Every move that ties for the best score (in move-generation order), and that score.
// No legal moves: an empty list and the mate/stalemate score.
inline std::tuple<MoveList, int> bestMoves(const Position& root, int depth) {
    Position pos = root;
    MoveList moves{};
    generateLegalMoves(pos, moves);
    if (moves.empty()) return {MoveList{}, pos.inCheck() ? -Engine::MATE_CPAWNS : 0};

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
    return {best_moves, best};
}

}  // namespace rs::reference
