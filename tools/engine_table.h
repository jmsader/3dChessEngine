// Engine table shared by the command-line tools (match, gendata): how to create each engine
// from its name. Add new engines here (one line each); every tool then accepts them.
#pragma once

#include <cctype>
#include <functional>
#include <memory>
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

// Options with the given switches and the match's time control.
rs::MinimaxOptions minimaxOptions(bool pruning, bool ordering, bool qscence = false, bool tt = false) {
    rs::MinimaxOptions o;
    o.pruning = pruning;
    o.ordering = ordering;
    o.qscence = qscence;
    o.tt = tt;
    if (g_msPerMove > 0) o.ms_per_move = g_msPerMove;
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

// Creates an engine from its name (case-insensitive), e.g. "Random", "alpha-beta+ord".
std::unique_ptr<rs::Engine> makeEngine(const std::string& name) {
    for (const EngineFactory& make : engineTable()) {
        auto engine = make();
        if (lower(engine->name) == lower(name)) return engine;
    }
    return nullptr;
}

// "Random Minimax Alpha-Beta ... Alpha-Beta+Ord+Q+TT", for usage messages.
std::string availableEngines() {
    std::string out;
    for (const EngineFactory& make : engineTable()) out += " " + make()->name;
    return out;
}

}  // namespace
