// Position: the board, side to move, move counters, hash key and undo history.
//
// The class owns all mutable game state and keeps three things incrementally in
// sync with the board: the Zobrist key, the king squares and the halfmove clock.
// checkConsistency() recomputes all of them from scratch for testing.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "raumschach/types.h"

namespace rs {

class Position {
public:
    // Text format (see README): levels A..E separated by '|', ranks 1..5 within a
    // level separated by '/', files a..e within a rank; digits = runs of empty cells;
    // White uppercase, Black lowercase; then side to move, halfmove clock, fullmove number.
    static constexpr const char* kStartText =
        "RNKNR/PPPPP/5/5/5|BUQBU/PPPPP/5/5/5|5/5/5/5/5|5/5/5/ppppp/ubqub|5/5/5/ppppp/rnknr w 0 1";

    Position();  // the standard starting position

    // Parses and validates a position. On failure returns false, leaves *this
    // unchanged and (if error is non-null) describes the problem.
    bool setFromText(const std::string& text, std::string* error = nullptr);
    std::string toText() const;
    std::string diagram() const;  // human-readable board, five levels side by side

    // ----- Queries
    Piece pieceAt(Square s) const { return board_[static_cast<std::size_t>(s)]; }
    Color sideToMove() const { return sideToMove_; }
    int halfmoveClock() const { return halfmoveClock_; }
    int fullmoveNumber() const { return fullmoveNumber_; }
    std::uint64_t key() const { return key_; }
    Square kingSquare(Color c) const { return kingSquare_[c]; }
    int movesPlayed() const { return static_cast<int>(history_.size()); }
    Move lastMove() const { return history_.empty() ? Move() : history_.back().move; }
    Piece lastCaptured() const { return history_.empty() ? NoPiece : history_.back().captured; }

    bool isAttacked(Square s, Color by) const;
    bool inCheck() const { return isAttacked(kingSquare_[sideToMove_], ~sideToMove_); }

    // Number of times the current position has occurred since the last irreversible
    // move, counting the current occurrence (so >= 1). Only positions reached through
    // makeMove() are remembered.
    int repetitionCount() const;

    // ----- Making moves
    // makeMove requires a pseudo-legal move for the side to move (from the move
    // generator). It does not check legality; see isLegal / generateLegalMoves.
    void makeMove(Move m);
    void unmakeMove();  // undoes the most recent makeMove
    // True if the pseudo-legal move m does not leave the mover's own king attacked.
    bool isLegal(Move m);

    // ----- Testing support
    std::uint64_t computeKey() const;  // from scratch
    bool checkConsistency(std::string* why = nullptr) const;
    bool samePlacementAndState(const Position& other) const;  // ignores history

private:
    struct Undo {
        Move move;
        Piece captured;
        int halfmoveClock;   // before the move
        std::uint64_t key;   // before the move
    };

    struct EmptyTag {};
    explicit Position(EmptyTag) { clear(); }  // empty board; used while parsing

    void clear();
    void put(Square s, Piece p);  // setup helper; maintains key and king squares

    std::array<Piece, kNumSquares> board_{};
    Color sideToMove_ = White;
    int halfmoveClock_ = 0;
    int fullmoveNumber_ = 1;
    std::uint64_t key_ = 0;
    std::array<Square, kNumColors> kingSquare_{kNoSquare, kNoSquare};
    std::vector<Undo> history_;
};

}  // namespace rs
