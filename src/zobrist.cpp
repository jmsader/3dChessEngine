#include "raumschach/zobrist.h"

#include "raumschach/rng.h"

namespace rs {

namespace {

ZobristKeys buildKeys() {
    ZobristKeys keys;
    Rng rng(0x5241554D53434841ull);  // "RAUMSCHA"
    for (auto& perPiece : keys.pieceSquare)
        for (auto& key : perPiece) key = rng.next();
    // The empty "piece" must never change the hash.
    for (auto& key : keys.pieceSquare[NoPiece]) key = 0;
    keys.blackToMove = rng.next();
    return keys;
}

}  // namespace

const ZobristKeys& zobrist() {
    static const ZobristKeys instance = buildKeys();
    return instance;
}

}  // namespace rs
