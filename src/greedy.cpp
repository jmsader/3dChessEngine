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
  Move best_move{};
  int best_cpawns = INT_MIN;
  for (Move move : moves) {
    _pos.makeMove(move);
    int move_cpawns = -eval_material(_pos);
    if (move_cpawns > best_cpawns) {
      best_move = move;
      best_cpawns = move_cpawns;
    }
    _pos.unmakeMove();
  }

  return {best_move, best_cpawns};
}

} // namespace rs