#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/rng.h"
#include <tuple>

namespace rs {

Minimax::Minimax(int max_depth) : Engine("Minimax" + std::to_string(max_depth)), max_depth_(max_depth) {}

int Minimax::negamax(Position &pos_nd, int depth, int ply) {
  MoveList moves{};
  generateLegalMoves(pos_nd, moves);
  if (moves.empty())
    return pos_nd.inCheck() ? -(MATE_CPAWNS - ply) : 0;
  if (depth == 0)
    return eval_material(pos_nd);

  int best = -INF_CPAWNS;
  for (Move move : moves) {
    pos_nd.makeMove(move);
    const int score = -negamax(pos_nd, depth - 1, ply + 1);
    pos_nd.unmakeMove();
    if (score > best)
      best = score;
  }

  return best;
}

std::tuple<Move, int> Minimax::search(const Position &pos) {
  Position pos_root = pos; // one copy per search
  MoveList moves{};
  generateLegalMoves(pos_root, moves);
  if (moves.empty())
    return {Move{}, pos.inCheck() ? -MATE_CPAWNS : 0};

  MoveList best_moves;
  int best_cpawns = -INF_CPAWNS;
  for (Move move : moves) {
    pos_root.makeMove(move);
    const int cand_cpawns = -negamax(pos_root, max_depth_ - 1, 1);
    pos_root.unmakeMove();
    if (cand_cpawns > best_cpawns) {
      best_cpawns = cand_cpawns;
      best_moves.clear();
      best_moves.push(move);
    } else if (cand_cpawns == best_cpawns) {
      best_moves.push(move);
    }
  }
  Move best_move = best_moves[rng.below(best_moves.size())];

  return {best_move, best_cpawns};
}

} // namespace rs