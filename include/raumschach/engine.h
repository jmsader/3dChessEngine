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
  static int eval_material(const Position &pos);
};

class Greedy : public Engine {
public:
  Greedy();
  std::tuple<Move, int> search(const Position &pos) override;
};

class Random : public Engine {
public:
  Random();
  std::tuple<Move, int> search(const Position &pos) override;
};

} // namespace rs