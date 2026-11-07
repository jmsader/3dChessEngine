#include "raumschach/geometry.h"

#include <cstdlib>

namespace rs {

namespace {

int nonZeroCount(const Vec3& v) {
    return (v.dx != 0) + (v.dy != 0) + (v.dz != 0);
}

template <int N>
void pushSquare(SquareList<N>& list, Square s) {
    assert(list.count < N);
    list.sq[static_cast<std::size_t>(list.count++)] = static_cast<std::uint8_t>(s);
}

// Destination of stepping from s by v, or kNoSquare when it leaves the board.
Square step(Square s, const Vec3& v) {
    const int x = fileOf(s) + v.dx, y = rankOf(s) + v.dy, z = levelOf(s) + v.dz;
    return onBoard(x, y, z) ? makeSquare(x, y, z) : kNoSquare;
}

Tables buildTables() {
    Tables t;

    // Directions, grouped rook / bishop / unicorn so each slider uses a contiguous range.
    int n = 0;
    for (int group = 1; group <= 3; ++group)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const Vec3 v{dx, dy, dz};
                    if (nonZeroCount(v) == group) t.dirs[static_cast<std::size_t>(n++)] = v;
                }
    assert(n == kNumDirections);

    // Knight: a permutation of (±2, ±1, 0) -> 24 vectors.
    std::array<Vec3, 24> knightVecs{};
    int k = 0;
    for (int dz = -2; dz <= 2; ++dz)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int a = std::abs(dx), b = std::abs(dy), c = std::abs(dz);
                const bool isKnight = (a + b + c == 3) && (a == 0 || b == 0 || c == 0) &&
                                      (a == 2 || b == 2 || c == 2);
                if (isKnight) knightVecs[static_cast<std::size_t>(k++)] = Vec3{dx, dy, dz};
            }
    assert(k == 24);

    for (Square s = 0; s < kNumSquares; ++s) {
        const auto us = static_cast<std::size_t>(s);
        for (int d = 0; d < kNumDirections; ++d) {
            const auto ud = static_cast<std::size_t>(d);
            Square cur = step(s, t.dirs[ud]);
            while (cur != kNoSquare) {
                pushSquare(t.ray[ud][us], cur);
                cur = step(cur, t.dirs[ud]);
            }
            const Square kt = step(s, t.dirs[ud]);
            if (kt != kNoSquare) pushSquare(t.king[us], kt);
        }
        for (const Vec3& v : knightVecs) {
            const Square to = step(s, v);
            if (to != kNoSquare) pushSquare(t.knight[us], to);
        }

        // Pawns. White: forward = +y, vertical = +z. Black: forward = -y, vertical = -z.
        for (int c = 0; c < kNumColors; ++c) {
            const auto uc = static_cast<std::size_t>(c);
            const int f = (c == White) ? 1 : -1;
            t.pawnPush[uc][us][kPushForward] = step(s, Vec3{0, f, 0});
            t.pawnPush[uc][us][kPushVertical] = step(s, Vec3{0, 0, f});
            // Captures: diagonally forward (sideways + forward) or diagonally
            // vertical (sideways + up/down). The forward+vertical diagonal is NOT a capture.
            const Vec3 caps[4] = {{-1, f, 0}, {1, f, 0}, {-1, 0, f}, {1, 0, f}};
            for (const Vec3& v : caps) {
                const Square to = step(s, v);
                if (to != kNoSquare) pushSquare(t.pawnCapture[uc][us], to);
            }
        }
    }

    // Inverse pawn table, built from the forward table.
    for (int c = 0; c < kNumColors; ++c) {
        const auto uc = static_cast<std::size_t>(c);
        for (Square s = 0; s < kNumSquares; ++s)
            for (Square target : t.pawnCapture[uc][static_cast<std::size_t>(s)])
                pushSquare(t.pawnAttackers[uc][static_cast<std::size_t>(target)], s);
    }
    return t;
}

}  // namespace

const Tables& tables() {
    static const Tables instance = buildTables();
    return instance;
}

DirRange sliderDirections(PieceType t) {
    switch (t) {
        case Rook: return {kRookDirBegin, kRookDirEnd};
        case Bishop: return {kBishopDirBegin, kBishopDirEnd};
        case Unicorn: return {kUnicornDirBegin, kUnicornDirEnd};
        case Queen: return {0, kNumDirections};
        default: return {0, 0};
    }
}

std::string squareName(Square s) {
    if (s < 0 || s >= kNumSquares) return "--";
    std::string name;
    name += static_cast<char>('A' + levelOf(s));
    name += static_cast<char>('a' + fileOf(s));
    name += static_cast<char>('1' + rankOf(s));
    return name;
}

Square parseSquare(const std::string& text) {
    if (text.size() != 3) return kNoSquare;
    const int z = text[0] - 'A', x = text[1] - 'a', y = text[2] - '1';
    return onBoard(x, y, z) ? makeSquare(x, y, z) : kNoSquare;
}

}  // namespace rs
