#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include <tuple>

namespace rs {

Minimax::Minimax(int max_depth) : Engine("Minimax"), max_depth_(max_depth) {}

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

  Move best_move{};
  int best_cpawns = -INF_CPAWNS;
  for (Move move : moves) {
    pos_root.makeMove(move);
    const int cand_cpawns = -negamax(pos_root, max_depth_ - 1, 1);
    pos_root.unmakeMove();
    if (cand_cpawns > best_cpawns) {
      best_cpawns = cand_cpawns;
      best_move = move;
    }
  }

  return {best_move, best_cpawns};
}

} // namespace rs