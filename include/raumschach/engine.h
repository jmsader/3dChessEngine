#pragma once
#include "raumschach/position.h"
#include "raumschach/rng.h"
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace rs {

// One transposition-table slot: what the search learned about one position.
struct TTentry {
  std::uint64_t key =
      0;          // full pos.key(), to confirm the slot holds this position
  int depth = -1; // depth of the search that stored it (-1 = empty)
  int score =
      0; // mate scores stored as distance from this position, not the root
  bool is_exact = false; // score is exact (not a bound): safe to reuse
  Move best_move{};      // best move found; tried first next time
};

class Engine {
public:
  std::string name;
  // Positions the last search() visited (the root and every negamax call); 0
  // for engines that don't search. Tools read it to report nodes per move.
  std::uint64_t nodes = 0;
  // Deepest search depth the last search() completed; 0 for engines that don't
  // search. Tools read it to report average depth per move.
  int depth_reached = 0;
  // may not need

  Engine(std::string engine_name);
  virtual std::tuple<Move, int> search(const Position &pos) = 0;
  virtual ~Engine() = default;
  void seed(std::uint64_t s);

  static constexpr int INF_CPAWNS = 32000;
  static constexpr int MATE_CPAWNS = 30000;
  static constexpr int MAX_PLY = 100;
  static int eval_material(const Position &pos);
  static int piece_cpawns(PieceType pt); // value of one piece of this type
  static bool is_mate_cpawns(int cp);
  // Shared score scale and evaluation; public so tests and tools can use them
  // too.

protected:
  Rng rng{};
};

class Random : public Engine {
public:
  Random();
  std::tuple<Move, int> search(const Position &pos) override;
};

// Switches for the minimax search. Every search is iterative deepening under a
// time control (secs_per_move); the clock decides how deep it goes. `pruning`
// turns plain minimax into alpha-beta; each later step of the ladder
// (quiescence, transposition table) is added here as one more option, so one
// class covers every variant and the defaults always reproduce the plain
// engine.
struct MinimaxOptions {
  bool pruning = false;  // alpha-beta pruning; false = plain minimax (same
                         // result, no cutoffs)
  bool ordering = false; // MVV-LVA move ordering
  int ms_per_move = 100;
  int max_depth = 0; // > 0: search exactly this deep with no time limit
                     // (tools/gendata); 0 = the time control decides
  bool qscence = false;
  bool tt = false; // transposition table (best move first; reuse exact scores)
  std::string
      eval_weights_path; // weights file from tools/train.py; empty = material
};

class Minimax : public Engine {
public:
  explicit Minimax(MinimaxOptions opts = {});
  std::tuple<Move, int> search(const Position &pos) override;
  const MinimaxOptions &options() const { return opts_; }
  // The position's quiescence score for the side to move, with no time limit:
  // captures (and escapes from check) played out until the position is quiet.
  // tools/gendata uses it to score opening moves.
  int quiescenceScore(const Position &pos);

  // The engine's name for a set of options, e.g. "Minimax", "Alpha-Beta",
  // "Alpha-Beta+Ord+Q+TT".
  static std::string nameFor(const MinimaxOptions &opts);

private:
  MinimaxOptions opts_;
  // The leaf evaluation, chosen once in the constructor: eval_material, or a
  // Model loaded from opts_.eval_weights_path.
  std::function<int(const Position &)> evaluate_pos_;
  // The clock is read every CHECK_EVERY_NODES nodes (well under 0.1 ms);
  // once the time budget is used up, stopped_ is set and the search unwinds.
  static constexpr std::uint64_t CHECK_EVERY_NODES = 8;
  std::clock_t start_time_ = 0; // when the current search() started
  double time_limit_ms_ = 0.0;  // when to stop: ms_per_move, or none
  bool stopped_ = false;
  double elapsed_ms() const; // CPU time since start_time_
  bool out_of_time();        // sets and returns stopped_
  static constexpr std::size_t TT_SIZE = std::size_t{1}
                                         << 20; // slots (power of 2)
  std::vector<TTentry> tt_; // TT_SIZE slots when opts_.tt, else empty
  int negamax(Position &pos, int depth, int ply, int alpha, int beta);
  int quiescence(Position &pos, int ply, int alpha, int beta);
  int MVV_LVA(const Position &pos, Move move);
  static int scoreToTT(int score,
                       int ply); // mate score: root distance -> node distance
  static int scoreFromTT(int score, int ply); // and back
};

} // namespace rs