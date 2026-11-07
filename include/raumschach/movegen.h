// Move generation.
//
// Pseudo-legal generation follows piece movement rules only; legal generation
// additionally removes moves that leave the mover's king attacked (by making each
// move, testing, and unmaking it). Simple and robust first; a faster pin-aware
// generator can replace the filter later and be checked against this one with perft.
#pragma once

#include <cstdint>

#include "raumschach/types.h"

namespace rs {

class Position;

enum class GenType {
    All,    // every pseudo-legal move
    Noisy,  // captures and promotions only (for quiescence search)
};

void generatePseudoLegalMoves(const Position& pos, MoveList& out, GenType type = GenType::All);
void generateLegalMoves(Position& pos, MoveList& out, GenType type = GenType::All);

// Number of leaf nodes of the legal move tree to the given depth (depth 0 = 1).
std::uint64_t perft(Position& pos, int depth);

}  // namespace rs
