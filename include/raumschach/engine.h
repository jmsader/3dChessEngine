#pragma once
#include "raumschach/position.h"
#include "raumschach/rng.h"
#include <string>
#include <tuple>

namespace rs {

class Engine {
public:
  std::string name;
  // may not need

  Engine(std::string name);
  virtual std::tuple<Move, int> search(const Position &pos) = 0;
  virtual ~Engine() = default;
  void seed(std::uint64_t s);

  // Shared score scale and evaluation; public so tests and tools can use them too.
  static constexpr int INF_CPAWNS = 32000;
  static constexpr int MATE_CPAWNS = 30000;
  static constexpr int MAX_PLY = 100;
  static int eval_material(const Position &pos);
  static bool is_mate_cpawns(int cp);

protected:
  Rng rng{};
};

class Random : public Engine {
public:
  Random();
  std::tuple<Move, int> search(const Position &pos) override;
};

// Switches for the minimax search. `pruning` turns plain minimax into alpha-beta; each later
// step of the ladder (move ordering, iterative deepening, quiescence, transposition table) is
// added here as one more option, so one class covers every variant and the defaults always
// reproduce the plain engine.
struct MinimaxOptions {
  int depth = 3;        // search depth in plies
  bool pruning = true;  // alpha-beta pruning; false = plain minimax (same result, no cutoffs)
};

class Minimax : public Engine {
public:
  explicit Minimax(MinimaxOptions opts = {});
  std::tuple<Move, int> search(const Position &pos) override;
  const MinimaxOptions &options() const { return opts_; }

  // The engine's name for a set of options: "Alpha-Beta3", or "Minimax3" with pruning off.
  static std::string nameFor(const MinimaxOptions &opts);

private:
  MinimaxOptions opts_;
  int negamax(Position &pos, int depth, int ply, int alpha, int beta);
};

} // namespace rs