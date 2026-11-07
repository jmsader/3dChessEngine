// Game termination under the project ruleset (see RULES.md):
//   checkmate -> loss for the side to move
//   stalemate -> draw
//   bare kings (only the two kings left) -> draw
//   threefold repetition -> draw
//   50-move rule (100 half-moves with no capture or pawn move) -> draw
// Checkmate and stalemate take precedence over the draw rules, as in FIDE chess.
#pragma once

#include "raumschach/types.h"

namespace rs {

class Position;

enum class GameResult { Ongoing, WhiteWins, BlackWins, Draw };
enum class EndReason { None, Checkmate, Stalemate, BareKings, Repetition, FiftyMoveRule };

struct GameStatus {
    GameResult result = GameResult::Ongoing;
    EndReason reason = EndReason::None;
    bool isOver() const { return result != GameResult::Ongoing; }
};

GameStatus gameStatus(Position& pos);

// True when no pieces other than the two kings are on the board.
bool onlyKingsRemain(const Position& pos);

const char* toString(GameResult r);
const char* toString(EndReason r);

}  // namespace rs
