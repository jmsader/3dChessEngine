#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/position.h"
#include "raumschach/types.h"
#include <climits>
#include <tuple>

namespace rs {

Greedy::Greedy() : Engine("Greedy") {}

std::tuple<Move, int> Greedy::search(const Position &pos) {
  Position _pos = pos; // start pos
  MoveList moves{};
  generateLegalMoves(_pos, moves);
  if (moves.empty()) {
    return {Move{}, eval_material(_pos)};
  }
  MoveList best_moves;
  int best_cpawns = INT_MIN;
  for (Move move : moves) {
    _pos.makeMove(move);
    int move_cpawns = -eval_material(_pos);
    _pos.unmakeMove();
    if (move_cpawns > best_cpawns) {
      best_cpawns = move_cpawns;
      best_moves.clear();
      best_moves.push(move);
    } else if (move_cpawns == best_cpawns) {
      best_moves.push(move);
    }
  }
  Move best_move = best_moves[rng.below(best_moves.size())];

  return {best_move, best_cpawns};
}

} // namespace rs