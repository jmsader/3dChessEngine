#include "raumschach/engine.h"
#include "raumschach/model.h"
#include "raumschach/movegen.h"
#include "raumschach/rng.h"
#include "raumschach/types.h"
#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace rs {

namespace {
// Name suffix for the evaluation, from the weights file's name. tools/train.py
// writes <engine>_<ms>ms_linear.txt or <engine>_<ms>ms_mlp.txt, so e.g.
// weights/Alpha-Beta+Ord+Q_10ms_mlp.txt gives "+NN_10": the model kind and the
// time control of its training data. The constructor checks that the loaded
// file really is that kind of model.
std::string evalSuffix(const std::string &weights_path) {
  if (weights_path.empty())
    return ""; // material
  std::string suffix =
      weights_path.find("_mlp") != std::string::npos ? "+NN" : "+LIN";
  const std::size_t ms = weights_path.rfind("ms_");
  std::size_t digits = ms;
  while (ms != std::string::npos && digits > 0 &&
         weights_path[digits - 1] >= '0' && weights_path[digits - 1] <= '9')
    --digits;
  if (ms != std::string::npos && digits < ms && digits > 0 &&
      weights_path[digits - 1] == '_')
    suffix += "_" + weights_path.substr(digits, ms - digits);
  return suffix;
}
} // namespace

std::string Minimax::nameFor(const MinimaxOptions &opts) {
  // No depth in the name: the time control sets the depth.
  return std::string(opts.pruning ? "Alpha-Beta" : "Minimax") +
         (opts.ordering ? "+Ord" : "") + (opts.qscence ? "+Q" : "") +
         (opts.tt ? "+TT" : "") + evalSuffix(opts.eval_weights_path);
}

Minimax::Minimax(MinimaxOptions opts) : Engine(nameFor(opts)), opts_(opts) {
  if (opts_.tt)
    tt_.assign(TT_SIZE, TTentry{}); // allocated once; kept between moves
  // Choose the evaluation once: the learned model (loaded here, not per call)
  // or plain material.
  if (opts_.eval_weights_path.empty()) {
    evaluate_pos_ = &Engine::eval_material;
  } else {
    Model model(opts_.eval_weights_path);
    // nameFor named the evaluation from the file name; make sure it's right.
    const bool named_linear =
        evalSuffix(opts_.eval_weights_path).rfind("+LIN", 0) == 0;
    if (named_linear != (model.num_layers() == 1))
      throw std::runtime_error("weights file " + opts_.eval_weights_path +
                               (named_linear ? " is named like a linear model"
                                             : " is named like an MLP") +
                               " but has " +
                               std::to_string(model.num_layers()) + " layers");
    evaluate_pos_ = std::move(model);
  }
}

// Mate scores count plies from the root (MATE - ply), but a table entry can be
// read at a different ply. So they are stored as distance from the position
// itself (scoreToTT) and turned back into root distance when read
// (scoreFromTT). Other scores don't depend on the ply and pass through
// unchanged.
int Minimax::scoreToTT(int score, int ply) {
  if (is_mate_cpawns(score))
    return score > 0 ? score + ply : score - ply;
  return score;
}

int Minimax::scoreFromTT(int score, int ply) {
  if (is_mate_cpawns(score))
    return score > 0 ? score - ply : score + ply;
  return score;
}

// Moves `move` to the front of `moves`, keeping the others in order. Does
// nothing if it isn't there (e.g. a stale table entry).
static void moveToFront(MoveList &moves, Move move) {
  Move *it = std::find(moves.begin(), moves.end(), move);
  if (it != moves.end())
    std::rotate(moves.begin(), it, it + 1);
}

int Minimax::negamax(Position &pos_nd, int depth, int ply, int alpha,
                     int beta) {
  ++nodes;
  const int alpha_orig =
      alpha; // to tell an exact score from a bound at the end

  // TT probe: reuse an exact score from a search at least this deep; otherwise
  // remember the stored best move to try it first.
  TTentry *entry = nullptr;
  Move tt_move{};
  if (opts_.tt) {
    entry = &tt_[pos_nd.key() & (TT_SIZE - 1)];
    if (entry->key == pos_nd.key()) {
      if (entry->is_exact && entry->depth >= depth)
        return scoreFromTT(entry->score, ply);
      tt_move = entry->best_move;
    }
  }

  MoveList moves{};
  generateLegalMoves(pos_nd, moves);
  if (moves.empty()) {
    int forced_score = pos_nd.inCheck() ? -(MATE_CPAWNS - ply) : 0;
    return forced_score;
  }
  if (depth == 0) {
    return opts_.qscence ? quiescence(pos_nd, alpha, beta)
                         : evaluate_pos_(pos_nd);
  }

  if (opts_.ordering) {
    std::stable_sort(moves.begin(), moves.end(),
                     [&pos_nd, this](Move a, Move b) {
                       return MVV_LVA(pos_nd, a) > MVV_LVA(pos_nd, b);
                     });
  }
  if (!tt_move.isNull())
    moveToFront(moves, tt_move); // the TT move goes first, before MVV-LVA order

  int best_score = -INF_CPAWNS;
  Move best_move{};
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
    if (score > best_score) {
      best_score = score;
      best_move = move;
    }
    if (score > alpha) {
      alpha = score;
    }
  }

  // TT store (always replace). The score is exact only if it landed strictly
  // inside the window this node was called with.
  if (opts_.tt) {
    const bool exact = best_score > alpha_orig && best_score < beta;
    *entry = TTentry{pos_nd.key(), depth, scoreToTT(best_score, ply), exact,
                     best_move};
  }
  return alpha;
}

std::tuple<Move, int> Minimax::search(const Position &pos) {
  Position pos_root = pos; // one copy per search
  nodes = 1;               // the root; negamax counts the rest
  depth_reached = 0;
  ms_used = 0.0;
  // The moves (all equally good) and score of the last depth that finished
  // within the time budget; a depth that finishes over it is thrown away.
  MoveList best_moves;
  int best_cpawns = 0;
  int alpha =
      0; // read by the loop condition before the first depth (0 = not a mate)
  int iter_depth = 1;
  const std::clock_t start_time = std::clock();
  const auto elapsed_ms = [start_time] { // CPU time since the search started
    return static_cast<double>(std::clock() - start_time) * 1000.0 /
           CLOCKS_PER_SEC;
  };
  // Start a new depth only if it is predicted to finish within the time
  // budget: it should take about as long as the last depth, times how much
  // the last depth grew over the one before (capped, since the first depths
  // are too short to measure reliably).
  double prev_depth_ms = 0.0, last_depth_ms = 0.0;
  const auto next_depth_fits = [&] {
    const double growth = prev_depth_ms > 0.0
                              ? std::min(last_depth_ms / prev_depth_ms, MAX_GROWTH)
                              : MAX_GROWTH;
    return elapsed_ms() + last_depth_ms * growth <= opts_.ms_per_move;
  };
  while ((iter_depth == 1 || next_depth_fits()) && !is_mate_cpawns(alpha)) {
    const double depth_start_ms = elapsed_ms();
    MoveList moves{};
    generateLegalMoves(pos_root, moves);
    if (moves.empty())
      return {Move{}, pos.inCheck() ? -MATE_CPAWNS : 0};

    if (opts_.ordering) {
      std::stable_sort(moves.begin(), moves.end(),
                       [&pos_root, this](Move a, Move b) {
                         return MVV_LVA(pos_root, a) > MVV_LVA(pos_root, b);
                       });
    }
    // the previous depth's best move goes first at the root.
    // if (!best_moves.empty())
    //   moveToFront(moves, best_moves[0]);

    MoveList depth_best_moves;
    alpha = -INF_CPAWNS;
    int beta = INF_CPAWNS;
    for (Move move : moves) {
      pos_root.makeMove(move);
      const int cand_cpawns =
          -negamax(pos_root, iter_depth - 1, 1, -beta, -(alpha - 1));
      pos_root.unmakeMove();
      if (cand_cpawns > alpha) {
        alpha = cand_cpawns;
        depth_best_moves.clear();
        depth_best_moves.push(move);
      } else if (cand_cpawns == alpha) {
        depth_best_moves.push(move);
      }
    }
    const double finished_ms = elapsed_ms();
    prev_depth_ms = last_depth_ms;
    last_depth_ms = finished_ms - depth_start_ms;
    if (finished_ms > opts_.ms_per_move)
      break; // finished over the budget: thrown away
    best_moves = depth_best_moves;
    best_cpawns = alpha;
    ms_used = finished_ms;
    depth_reached = iter_depth; // this depth finished in time
    ++iter_depth;
  }

  if (best_moves.empty()) { // no depth finished in time (ms_used stays 0)
    Random rand_eng;
    return rand_eng.search(pos);
  }

  Move best_move = best_moves[rng.below(best_moves.size())];

  return {best_move, best_cpawns};
}

int Minimax::quiescence(Position &pos, int alpha, int beta) {
  ++nodes;

  int curr_val = evaluate_pos_(pos);
  if (curr_val >= beta) {
    return curr_val;
  }
  if (curr_val > alpha) {
    alpha = curr_val;
  }

  MoveList captures;
  generateLegalMoves(pos, captures, GenType::Noisy);
  if (opts_.ordering)
    std::stable_sort(captures.begin(), captures.end(),
                     [&pos, this](Move x, Move y) {
                       return MVV_LVA(pos, x) > MVV_LVA(pos, y);
                     });

  for (const Move move : captures) {
    pos.makeMove(move);
    int score = -quiescence(pos, -beta, -alpha);
    pos.unmakeMove();
    if (score >= beta) {
      return score;
    }
    if (score > alpha) {
      alpha = score;
    }
  }
  return alpha;
}

int Minimax::MVV_LVA(const Position &pos, Move move) {
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

} // namespace rs