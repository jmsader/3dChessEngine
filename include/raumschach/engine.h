#pragma once
#include "raumschach/position.h"
#include "raumschach/rng.h"
#include <cstdint>
#include <string>
#include <tuple>

namespace rs {

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

  Engine(std::string name);
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
  bool qscence = false;
};

class Minimax : public Engine {
public:
  explicit Minimax(MinimaxOptions opts = {});
  std::tuple<Move, int> search(const Position &pos) override;
  const MinimaxOptions &options() const { return opts_; }

  // The engine's name for a set of options: "Minimax", "Alpha-Beta" or
  // "Alpha-Beta+Ord".
  static std::string nameFor(const MinimaxOptions &opts);

private:
  MinimaxOptions opts_;
  int negamax(Position &pos, int depth, int ply, int alpha, int beta);
  int quiescence(Position &pos, int alpha, int beta);
  int MVV_LVA(const Position &pos, Move move);
};

} // namespace rs