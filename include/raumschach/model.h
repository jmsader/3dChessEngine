#pragma once
// A learned evaluation trained by tools/train.py: a stack of fully connected
// layers with ReLU between them. One layer is the linear model (piece-square
// tables); more layers are the MLP.
//
// Built entirely by its constructor (reads and checks the weights file once),
// then used only for inference: calling it on a position returns centipawns
// for the side to move, like Engine::eval_material.
#include "raumschach/position.h"
#include <cstddef>
#include <string>
#include <vector>

namespace rs {

class Model {
public:
  // Reads a weights file written by export_weights in tools/train.py.
  // Throws std::runtime_error if it can't be opened or has the wrong shape.
  explicit Model(const std::string &path);

  // Inference: centipawns for the side to move.
  int operator()(const Position &pos) const;

  // Number of layers: 1 = the linear model, more = an MLP.
  std::size_t num_layers() const { return layers_.size(); }

private:
  // Input encoding, which must match encode() in tools/train.py:
  //   feature = owner * 7*125 + type * 125 + square
  //   owner 0 = side to move, 1 = opponent; type 0..6 = P N B R U Q K;
  //   square flipped through the centre (124 - sq) when Black is to move.
  static constexpr int NUM_FEATURES = 2 * 7 * kNumSquares; // 1750

  // Largest number of OUTPUTS any layer may have. Inference keeps one layer's
  // outputs in fixed-size stack arrays (no allocation per evaluation). It does
  // not limit the first layer's 1750 inputs, which are added straight into
  // that layer's outputs.
  static constexpr int MAX_LAYER_OUTPUTS = 128;

  struct Layer {
    int num_inputs = 0;      // PyTorch in_features (1750 for the first layer)
    int num_outputs = 0;     // PyTorch out_features (1 for the last layer)
    std::vector<float> bias; // num_outputs
    std::vector<float>
        weight; // num_outputs x num_inputs, row by row (as exported)
  };
  std::vector<Layer> layers_;
};

} // namespace rs
