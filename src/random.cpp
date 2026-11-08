#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/position.h"
#include "raumschach/types.h"
#include <cstdlib>
#include <tuple>

namespace rs {

Random::Random() : Engine("Random") {}

std::tuple<Move, int> Random::search(const Position &pos) {
  Position _pos = pos; // start pos
  MoveList moves{};
  generateLegalMoves(_pos, moves);
  if (moves.empty()) {
    return {Move{}, eval_material(_pos)};
  }
  int move_randidx = std::rand() % moves.size();
  Move rand_move = moves[move_randidx];
  _pos.makeMove(rand_move);
  int move_cpawns = -eval_material(_pos);
  _pos.unmakeMove();

  return {rand_move, move_cpawns};
}

} // namespace rs