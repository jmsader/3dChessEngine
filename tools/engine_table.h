// Engine table shared by the command-line tools (match, gendata): how to create each engine
// from its name. Add new engines here (one line each); every tool then accepts them.
#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "raumschach/engine.h"

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// How to create each engine. Add new engines here (one line each). The name you type, and the
// name shown in all output, is the engine's own `name` member, so renaming an engine in its
// constructor updates the match tool automatically.
using EngineFactory = std::function<std::unique_ptr<rs::Engine>()>;

int g_msPerMove = 0;  // from --time, in milliseconds; 0 = keep MinimaxOptions' default
int g_maxDepth = 0;   // from gendata's --depth: fixed search depth, no time limit; 0 = timed

// Learned evaluations: an engine name ending in "+LIN_d<depth>" or "+NN_d<depth>" is that search
// engine with the linear / MLP model trained on data searched to <depth>, e.g.
// "Alpha-Beta+Ord+Q+NN_d2" uses weights/Alpha-Beta+Ord+Q_d2_mlp.txt. All training data comes from
// kDataEngine.
const std::string kWeightsDir = "weights/";
const std::string kDataEngine = "Alpha-Beta+Ord+Q";
const int kAllDepth = 2;  // match --all uses the models trained on depth-kAllDepth data
std::string g_evalWeights;  // set by makeEngine only while it creates a +LIN_/+NN_ engine

// Options with the given switches and the match's time control.
rs::MinimaxOptions minimaxOptions(bool pruning, bool ordering, bool qscence = false, bool tt = false) {
    rs::MinimaxOptions o;
    o.pruning = pruning;
    o.ordering = ordering;
    o.qscence = qscence;
    o.tt = tt;
    if (g_msPerMove > 0) o.ms_per_move = g_msPerMove;
    o.max_depth = g_maxDepth;
    o.eval_weights_path = g_evalWeights;
    return o;
}

const std::vector<EngineFactory>& engineTable() {
    static const std::vector<EngineFactory> table = {
        [] { return std::make_unique<rs::Random>(); },
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(false, false)); },  // Minimax
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, false)); },   // Alpha-Beta
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, true)); },    // Alpha-Beta+Ord
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, true, true)); },  // Alpha-Beta+Ord+Q
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, true, true, true)); },  // Alpha-Beta+Ord+Q+TT
    };
    return table;
}

// The weights file for a "+LIN_d<depth>" / "+NN_d<depth>" suffix, or "" if `name` has no such
// suffix.
// Sets `base` to the name without the suffix (lowercase).
std::string weightsFileFor(const std::string& name, std::string& base) {
    base = lower(name);
    const std::size_t plus = base.rfind('+');
    if (plus == std::string::npos) return "";
    const std::string suffix = base.substr(plus + 1);  // e.g. "lin_d2"
    const std::size_t underscore = suffix.find('_');
    if (underscore == std::string::npos) return "";
    const std::string kind = suffix.substr(0, underscore), depth = suffix.substr(underscore + 1);  // "d2"
    if ((kind != "lin" && kind != "nn") || depth.size() < 2 || depth[0] != 'd' ||
        !std::all_of(depth.begin() + 1, depth.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return "";
    base.resize(plus);
    return kWeightsDir + kDataEngine + "_" + depth + "_" + (kind == "lin" ? "linear" : "mlp") + ".txt";
}

// Creates an engine from its name (case-insensitive), e.g. "Random", "alpha-beta+ord",
// "Alpha-Beta+Ord+Q+NN_d2". Returns nullptr for an unknown name; throws std::runtime_error if a
// +LIN_ / +NN_ engine's weights file is missing or can't be used.
std::unique_ptr<rs::Engine> makeEngine(const std::string& name) {
    std::string base;
    const std::string weights = weightsFileFor(name, base);
    for (const EngineFactory& make : engineTable()) {
        if (lower(make()->name) != base) continue;
        if (weights.empty()) return make();
        g_evalWeights = weights;  // this one engine gets the learned evaluation
        std::unique_ptr<rs::Engine> engine;
        try { engine = make(); } catch (...) { g_evalWeights.clear(); throw; }
        g_evalWeights.clear();
        if (lower(engine->name) != lower(name))  // e.g. "Random+LIN_d2"
            throw std::runtime_error(name + ": " + make()->name + " has no evaluation to replace");
        return engine;
    }
    return nullptr;
}

// "Random Minimax Alpha-Beta ... Alpha-Beta+Ord+Q+TT", for usage messages.
std::string availableEngines() {
    std::string out;
    for (const EngineFactory& make : engineTable()) out += " " + make()->name;
    return out + " (add +LIN_d<depth> / +NN_d<depth> to a search engine for a learned evaluation, e.g. "
                 "Alpha-Beta+Ord+Q+NN_d2 uses " + kWeightsDir + kDataEngine + "_d2_mlp.txt)";
}

}  // namespace
