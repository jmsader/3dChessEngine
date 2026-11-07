// Basic value types shared by the whole engine: colors, pieces, squares, moves.
//
// Coordinate convention (see RULES.md):
//   x = file  a..e -> 0..4
//   y = rank  1..5 -> 0..4   (White moves "forward" toward +y)
//   z = level A..E -> 0..4   (White moves "up" toward +z)
//   square index = z * 25 + y * 5 + x   (0..124)
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace rs {

constexpr int kSize = 5;                              // cells per axis
constexpr int kNumSquares = kSize * kSize * kSize;    // 125

// ---------------------------------------------------------------- Color
enum Color : std::uint8_t { White = 0, Black = 1 };
constexpr int kNumColors = 2;

constexpr Color operator~(Color c) { return Color(c ^ 1); }

// ---------------------------------------------------------------- Pieces
enum PieceType : std::uint8_t {
    NoPieceType = 0,
    Pawn = 1,
    Knight = 2,
    Bishop = 3,
    Rook = 4,
    Unicorn = 5,
    Queen = 6,
    King = 7,
};
constexpr int kNumPieceTypes = 8;  // including NoPieceType, for table sizing

// Piece = color * 8 + type. 0 is the empty square.
enum Piece : std::uint8_t {
    NoPiece = 0,
    WPawn = 1, WKnight, WBishop, WRook, WUnicorn, WQueen, WKing,
    BPawn = 9, BKnight, BBishop, BRook, BUnicorn, BQueen, BKing,
};
constexpr int kNumPieceCodes = 16;  // table size for Piece-indexed arrays

constexpr Piece makePiece(Color c, PieceType t) { return Piece((c << 3) | t); }
constexpr PieceType typeOf(Piece p) { return PieceType(p & 7); }
constexpr Color colorOf(Piece p) { return Color(p >> 3); }  // undefined for NoPiece

// ---------------------------------------------------------------- Squares
using Square = int;  // 0..124, or kNoSquare
constexpr Square kNoSquare = -1;

constexpr Square makeSquare(int x, int y, int z) { return z * 25 + y * 5 + x; }
constexpr int fileOf(Square s) { return s % 5; }         // x
constexpr int rankOf(Square s) { return (s / 5) % 5; }   // y
constexpr int levelOf(Square s) { return s / 25; }       // z
constexpr bool onBoard(int x, int y, int z) {
    return x >= 0 && x < kSize && y >= 0 && y < kSize && z >= 0 && z < kSize;
}

// ---------------------------------------------------------------- Moves
// Packed into 32 bits: from (bits 0-6), to (bits 7-13), promotion type (bits 14-16).
// The captured piece is NOT stored in the move; Position keeps it in its undo stack.
class Move {
public:
    constexpr Move() = default;
    constexpr Move(Square from, Square to, PieceType promo = NoPieceType)
        : data_(std::uint32_t(from) | (std::uint32_t(to) << 7) | (std::uint32_t(promo) << 14)) {}

    constexpr Square from() const { return Square(data_ & 0x7F); }
    constexpr Square to() const { return Square((data_ >> 7) & 0x7F); }
    constexpr PieceType promotion() const { return PieceType((data_ >> 14) & 0x7); }
    constexpr bool isNull() const { return data_ == 0; }  // from == to == 0 never occurs in a real move
    constexpr std::uint32_t raw() const { return data_; }

    constexpr bool operator==(Move o) const { return data_ == o.data_; }
    constexpr bool operator!=(Move o) const { return data_ != o.data_; }

private:
    std::uint32_t data_ = 0;
};

// Fixed-capacity move container (no heap allocation in the hot path).
// Capacity bound: 20 pieces per side; even if every pawn promoted to a queen
// (11 queens x 52 + 2 rooks x 12 + 2 bishops x 24 + 2 unicorns x 16 + 2 knights x 24 + king 26)
// the total is < 800, so 1024 is safe.
class MoveList {
public:
    static constexpr std::size_t kCapacity = 1024;

    void push(Move m) {
        assert(size_ < kCapacity);
        moves_[size_++] = m;
    }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { size_ = 0; }
    Move operator[](std::size_t i) const { return moves_[i]; }
    Move& operator[](std::size_t i) { return moves_[i]; }
    const Move* begin() const { return moves_.data(); }
    const Move* end() const { return moves_.data() + size_; }
    Move* begin() { return moves_.data(); }
    Move* end() { return moves_.data() + size_; }
    bool contains(Move m) const {
        for (Move x : *this)
            if (x == m) return true;
        return false;
    }

private:
    std::array<Move, kCapacity> moves_{};
    std::size_t size_ = 0;
};

}  // namespace rs
