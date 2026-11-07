// Precomputed board geometry: direction vectors, rays, knight/king/pawn targets.
//
// All tables are built once (thread-safe static initialisation) and then
// read-only, so move generation never has to do bounds checks.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "raumschach/types.h"

namespace rs {

struct Vec3 {
    int dx, dy, dz;
};

// The 26 queen/king directions, grouped by the piece that slides along them:
//   [ 0,  6)  rook     : exactly one non-zero component        (6)
//   [ 6, 18)  bishop   : exactly two non-zero components       (12)
//   [18, 26)  unicorn  : all three components non-zero         (8)
constexpr int kNumDirections = 26;
constexpr int kRookDirBegin = 0, kRookDirEnd = 6;
constexpr int kBishopDirBegin = 6, kBishopDirEnd = 18;
constexpr int kUnicornDirBegin = 18, kUnicornDirEnd = 26;

// Direction range [begin, end) a sliding piece type uses; {0,0} for non-sliders.
struct DirRange {
    int begin, end;
};
DirRange sliderDirections(PieceType t);

// A short fixed-capacity list of squares (used for rays and jump targets).
template <int N>
struct SquareList {
    std::array<std::uint8_t, N> sq{};  // square indices 0..124
    std::uint8_t count = 0;
    const std::uint8_t* begin() const { return sq.data(); }
    const std::uint8_t* end() const { return sq.data() + count; }
};

// Pawn "push" kinds, in the order they are stored.
constexpr int kPushForward = 0;  // one step along rank (+y White, -y Black)
constexpr int kPushVertical = 1; // one step along level (+z White, -z Black)

struct Tables {
    std::array<Vec3, kNumDirections> dirs{};

    // ray[d][s]: squares reached from s stepping repeatedly in direction d, nearest first.
    std::array<std::array<SquareList<kSize - 1>, kNumSquares>, kNumDirections> ray{};

    std::array<SquareList<24>, kNumSquares> knight{};  // knight targets
    std::array<SquareList<26>, kNumSquares> king{};    // king targets

    // pawnPush[c][s][k]: destination of push kind k, or kNoSquare if off board.
    std::array<std::array<std::array<Square, 2>, kNumSquares>, kNumColors> pawnPush{};
    // pawnCapture[c][s]: squares a pawn of color c standing on s attacks.
    std::array<std::array<SquareList<4>, kNumSquares>, kNumColors> pawnCapture{};
    // pawnAttackers[c][s]: squares from which a pawn of color c would attack s
    // (the inverse of pawnCapture, built explicitly rather than assumed symmetric).
    std::array<std::array<SquareList<4>, kNumSquares>, kNumColors> pawnAttackers{};
};

// The single shared table instance.
const Tables& tables();

// Promotion zone: White promotes on level E rank 5 (E*5), Black on level A rank 1 (A*1).
constexpr bool isPromotionSquare(Color c, Square s) {
    return c == White ? (levelOf(s) == 4 && rankOf(s) == 4)
                      : (levelOf(s) == 0 && rankOf(s) == 0);
}

// Square names: level letter (A-E), file letter (a-e), rank digit (1-5), e.g. "Ac2".
std::string squareName(Square s);
Square parseSquare(const std::string& text);  // kNoSquare on failure

}  // namespace rs
