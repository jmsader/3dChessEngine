#include "raumschach/model.h"
#include "raumschach/types.h"
#include <cmath>
#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace rs {

Model::Model(const std::string &path) {
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error("cannot open weights file: " + path);

  // File format (see export_weights in tools/train.py), for each layer in
  // order:
  //   "num_inputs num_outputs", then num_outputs biases, then
  //   num_outputs * num_inputs weights row by row.
  Layer layer;
  while (file >> layer.num_inputs >> layer.num_outputs) {
    layer.bias.resize(static_cast<std::size_t>(layer.num_outputs));
    for (float &b : layer.bias)
      file >> b;
    layer.weight.resize(static_cast<std::size_t>(layer.num_outputs) *
                        static_cast<std::size_t>(layer.num_inputs));
    for (float &w : layer.weight)
      file >> w;
    if (!file)
      throw std::runtime_error("weights file ends inside a layer: " + path);
    layers_.push_back(layer);
  }

  // Shape checks: 1750 inputs, each layer feeding the next, one output.
  if (layers_.empty() || layers_.front().num_inputs != NUM_FEATURES ||
      layers_.back().num_outputs != 1)
    throw std::runtime_error("unexpected network shape in " + path);
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (i > 0 && layers_[i].num_inputs != layers_[i - 1].num_outputs)
      throw std::runtime_error("layer sizes don't chain in " + path);
    if (layers_[i].num_outputs > MAX_LAYER_OUTPUTS)
      throw std::runtime_error(
          "a layer has more than MAX_LAYER_OUTPUTS outputs in " + path);
  }
}

int Model::operator()(const Position &pos) const {
  float outputs[MAX_LAYER_OUTPUTS]; // the current layer's outputs
  float
      next_outputs[MAX_LAYER_OUTPUTS]; // the next layer's, while computing them

  // First layer, sparse: start from its biases and add the weight column of
  // every piece on the board (about 40 of the 1750 inputs are 1, the rest 0).
  const Layer &first = layers_.front();
  const std::size_t first_inputs = static_cast<std::size_t>(first.num_inputs);
  std::size_t num_current = static_cast<std::size_t>(first.num_outputs);
  for (std::size_t out = 0; out < num_current; ++out)
    outputs[out] = first.bias[out];

  const Color us = pos.sideToMove();
  for (Square sq = 0; sq < kNumSquares; ++sq) {
    const Piece piece = pos.pieceAt(sq);
    if (typeOf(piece) == NoPieceType)
      continue;
    const int owner = colorOf(piece) == us ? 0 : 1;
    const int type = typeOf(piece) - Pawn; // Pawn=1..King=7 -> 0..6
    const int square = us == Black ? kNumSquares - 1 - sq : sq;
    const std::size_t feature = static_cast<std::size_t>(
        owner * 7 * kNumSquares + type * kNumSquares + square);
    for (std::size_t out = 0; out < num_current; ++out)
      outputs[out] += first.weight[out * first_inputs + feature];
  }

  // Remaining layers, dense, each after a ReLU. (None for the linear model.)
  for (std::size_t l = 1; l < layers_.size(); ++l) {
    const Layer &layer = layers_[l];
    const std::size_t layer_inputs = static_cast<std::size_t>(layer.num_inputs);
    const std::size_t layer_outputs =
        static_cast<std::size_t>(layer.num_outputs);
    for (std::size_t in = 0; in < num_current; ++in)
      outputs[in] = outputs[in] > 0.0f ? outputs[in] : 0.0f; // ReLU
    for (std::size_t out = 0; out < layer_outputs; ++out) {
      float sum = layer.bias[out];
      const float *row = &layer.weight[out * layer_inputs];
      for (std::size_t in = 0; in < layer_inputs; ++in)
        sum += row[in] * outputs[in];
      next_outputs[out] = sum;
    }
    num_current = layer_outputs;
    for (std::size_t out = 0; out < num_current; ++out)
      outputs[out] = next_outputs[out];
  }

  // The last layer was multiplied by K at export, so this is in centipawns.
  return static_cast<int>(std::lround(outputs[0]));
}

} // namespace rs
