#include "raumschach/rules.h"

#include "raumschach/config.h"
#include "raumschach/movegen.h"
#include "raumschach/position.h"

namespace rs {

GameStatus gameStatus(Position& pos) {
    MoveList moves;
    generateLegalMoves(pos, moves);
    if (moves.empty()) {
        if (pos.inCheck()) {
            const GameResult winner =
                pos.sideToMove() == White ? GameResult::BlackWins : GameResult::WhiteWins;
            return {winner, EndReason::Checkmate};
        }
        return {GameResult::Draw, EndReason::Stalemate};
    }
    if (config::kBareKingsDraw && onlyKingsRemain(pos))
        return {GameResult::Draw, EndReason::BareKings};
    if (pos.repetitionCount() >= config::kRepetitionDrawCount)
        return {GameResult::Draw, EndReason::Repetition};
    if (pos.halfmoveClock() >= config::kFiftyMoveHalfmoves)
        return {GameResult::Draw, EndReason::FiftyMoveRule};
    return {};
}

bool onlyKingsRemain(const Position& pos) {
    for (Square s = 0; s < kNumSquares; ++s) {
        const Piece p = pos.pieceAt(s);
        if (p != NoPiece && typeOf(p) != King) return false;
    }
    return true;
}

const char* toString(GameResult r) {
    switch (r) {
        case GameResult::Ongoing: return "ongoing";
        case GameResult::WhiteWins: return "1-0";
        case GameResult::BlackWins: return "0-1";
        case GameResult::Draw: return "1/2-1/2";
    }
    return "?";
}

const char* toString(EndReason r) {
    switch (r) {
        case EndReason::None: return "none";
        case EndReason::Checkmate: return "checkmate";
        case EndReason::Stalemate: return "stalemate";
        case EndReason::BareKings: return "bare kings";
        case EndReason::Repetition: return "threefold repetition";
        case EndReason::FiftyMoveRule: return "50-move rule";
    }
    return "?";
}

}  // namespace rs
