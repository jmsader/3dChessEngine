// Rule options that the project's ruleset fixes. Changing one of these changes
// the game being played, so record any change in RULES.md and in the write-up.
#pragma once

namespace rs::config {

// Promotion choices. false: queen, rook, bishop, knight or unicorn. true: queen only.
constexpr bool kQueenOnlyPromotion = false;

// A game is drawn when this many half-moves pass with no capture and no pawn move
// (100 half-moves = the standard 50-move rule).
constexpr int kFiftyMoveHalfmoves = 100;

// A game is drawn when the same position (same placement, same side to move) occurs this often.
constexpr int kRepetitionDrawCount = 3;

// A game is drawn as soon as only the two kings remain (mate is then impossible).
constexpr bool kBareKingsDraw = true;

}  // namespace rs::config
