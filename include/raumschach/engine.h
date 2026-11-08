#pragma once
#include "raumschach/position.h"
#include <string>
#include <tuple>

namespace rs {

class Engine {
public:
  std::string name;

  Engine(std::string name);
  virtual std::tuple<Move, int> search(const Position &pos) = 0;
  virtual ~Engine() = default;

protected:
  static constexpr int INF_CPAWNS = 32000;
  static constexpr int MATE_CPAWNS = 30000;
  static constexpr int MAX_PLY = 100;

  static int eval_material(const Position &pos);
  static bool is_mate_cpawns(int cp);
};

class Greedy : public Engine {
public:
  Greedy();
  std::tuple<Move, int> search(const Position &pos) override;
  // stop copying a new pos in searches?
};

class Random : public Engine {
public:
  Random();
  std::tuple<Move, int> search(const Position &pos) override;
};

class Minimax : public Engine {
public:
  Minimax(int max_depth = 1);
  std::tuple<Move, int> search(const Position &pos) override;

private:
  int max_depth_;

  int negamax(Position &pos, int depth, int ply);
};

} // namespace rs