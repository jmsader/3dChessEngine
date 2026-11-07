// Zobrist hashing keys. A position's key is the XOR of one key per (piece, square)
// plus a side-to-move key when Black is to move. Raumschach has no castling and no
// en passant, so nothing else contributes.
#pragma once

#include <array>
#include <cstdint>

#include "raumschach/types.h"

namespace rs {

struct ZobristKeys {
    std::array<std::array<std::uint64_t, kNumSquares>, kNumPieceCodes> pieceSquare{};
    std::uint64_t blackToMove = 0;
};

// Generated once from a fixed seed, so keys are identical on every machine and run.
const ZobristKeys& zobrist();

}  // namespace rs
