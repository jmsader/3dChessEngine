#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/rng.h"
#include <tuple>

namespace rs {

AlphaBeta::AlphaBeta(int max_depth)
    : Engine("Alpha-Beta" + std::to_string(max_depth)), max_depth_(max_depth) {}

int AlphaBeta::negamax(Position &pos_nd, int depth, int ply, int alpha,
                       int beta) {
  MoveList moves{};
  generateLegalMoves(pos_nd, moves);
  if (moves.empty())
    return pos_nd.inCheck() ? -(MATE_CPAWNS - ply) : 0;
  if (depth == 0)
    return eval_material(pos_nd);

  for (Move move : moves) {
    if (alpha >= beta) {
      break;
    }
    pos_nd.makeMove(move);
    const int score = -negamax(pos_nd, depth - 1, ply + 1, -beta, -alpha);
    pos_nd.unmakeMove();
    if (score > alpha) {
      alpha = score;
    }
  }

  return alpha;
}

std::tuple<Move, int> AlphaBeta::search(const Position &pos) {
  Position pos_root = pos; // one copy per search
  MoveList moves{};
  generateLegalMoves(pos_root, moves);
  if (moves.empty())
    return {Move{}, pos.inCheck() ? -MATE_CPAWNS : 0};

  MoveList best_moves;
  // int best_cpawns = -INF_CPAWNS;
  int alpha = -INF_CPAWNS;
  int beta = INF_CPAWNS;
  for (Move move : moves) {
    pos_root.makeMove(move);
    const int cand_cpawns =
        -negamax(pos_root, max_depth_ - 1, 1, -beta, -(alpha - 1));
    pos_root.unmakeMove();
    if (cand_cpawns > alpha) {
      alpha = cand_cpawns;
      best_moves.clear();
      best_moves.push(move);
    } else if (cand_cpawns == alpha) {
      best_moves.push(move);
    }
  }
  Move best_move = best_moves[rng.below(best_moves.size())];

  return {best_move, alpha};
}

} // namespace rs