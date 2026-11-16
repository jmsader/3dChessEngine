#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/rng.h"
#include "raumschach/types.h"
#include <algorithm>
#include <ctime>
#include <tuple>

namespace rs {

namespace {
int MVV_LVA(const Position &pos, Move move) {
  int score = 0;
  PieceType piece_type_prom_to = move.promotion();
  PieceType victim_type = typeOf(pos.pieceAt(move.to()));
  PieceType att_type = typeOf(pos.pieceAt(move.from()));

  if (victim_type != NoPieceType) {
    score +=
        10 * Engine::piece_cpawns(victim_type) - Engine::piece_cpawns(att_type);
  }
  if (piece_type_prom_to != NoPieceType) {
    score += Engine::piece_cpawns(piece_type_prom_to);
  }

  return score;
}
} // namespace

std::string Minimax::nameFor(const MinimaxOptions &opts) {
  // No depth in the name: the time control sets the depth.
  return std::string(opts.pruning ? "Alpha-Beta" : "Minimax") +
         (opts.ordering ? "+Ord" : "");
}

Minimax::Minimax(MinimaxOptions opts) : Engine(nameFor(opts)), opts_(opts) {}

int Minimax::negamax(Position &pos_nd, int depth, int ply, int alpha,
                     int beta) {
  ++nodes;
  MoveList moves{};
  generateLegalMoves(pos_nd, moves);
  if (moves.empty())
    return pos_nd.inCheck() ? -(MATE_CPAWNS - ply) : 0;
  if (depth == 0)
    return eval_material(pos_nd);

  if (opts_.ordering) {
    std::stable_sort(moves.begin(), moves.end(), [&pos_nd](Move a, Move b) {
      return MVV_LVA(pos_nd, a) > MVV_LVA(pos_nd, b);
    });
  }

  for (Move move : moves) {
    if (opts_.pruning && alpha >= beta) {
      break;
    }
    pos_nd.makeMove(move);
    // Without pruning every child gets the full window, so every score is
    // exact (plain minimax).
    const int score =
        opts_.pruning
            ? -negamax(pos_nd, depth - 1, ply + 1, -beta, -alpha)
            : -negamax(pos_nd, depth - 1, ply + 1, -INF_CPAWNS, INF_CPAWNS);
    pos_nd.unmakeMove();
    if (score > alpha) {
      alpha = score;
    }
  }

  return alpha;
}

std::tuple<Move, int> Minimax::search(const Position &pos) {
  Position pos_root = pos; // one copy per search
  nodes = 1;               // the root; negamax counts the rest
  depth_reached = 0;
  MoveList best_moves;
  int alpha;
  int iter_depth = 1;
  const std::clock_t start_time = std::clock();
  while (int((std::clock() - start_time) / CLOCKS_PER_SEC) * 2 <
         opts_.secs_per_move) {
    MoveList moves{};
    generateLegalMoves(pos_root, moves);
    if (moves.empty())
      return {Move{}, pos.inCheck() ? -MATE_CPAWNS : 0};

    if (opts_.ordering) {
      std::stable_sort(moves.begin(), moves.end(), [&pos_root](Move a, Move b) {
        return MVV_LVA(pos_root, a) > MVV_LVA(pos_root, b);
      });
    }

    // int best_cpawns = -INF_CPAWNS;
    alpha = -INF_CPAWNS;
    int beta = INF_CPAWNS;
    for (Move move : moves) {
      pos_root.makeMove(move);
      const int cand_cpawns =
          -negamax(pos_root, iter_depth - 1, 1, -beta, -(alpha - 1));
      pos_root.unmakeMove();
      if (cand_cpawns > alpha) {
        alpha = cand_cpawns;
        best_moves.clear();
        best_moves.push(move);
      } else if (cand_cpawns == alpha) {
        best_moves.push(move);
      }
    }
    depth_reached = iter_depth; // this depth finished
    ++iter_depth;
  }

  if (best_moves.empty()) {
    Random rand_eng;
    return rand_eng.search(pos);
  }

  Move best_move = best_moves[rng.below(best_moves.size())];

  return {best_move, alpha};
}

} // namespace rs