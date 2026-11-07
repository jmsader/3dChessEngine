// Conversions between engine values and text.
//
// Move text: from-square + to-square + optional promotion letter, e.g. "Ac2Ac3",
// "Ec4Ec5q". Promotion letters: q r b n u.
#pragma once

#include <string>

#include "raumschach/types.h"

namespace rs {

class Position;

char pieceToChar(Piece p);  // 'P','N','B','R','U','Q','K' (White) / lowercase (Black); '.' for empty
Piece charToPiece(char c);  // NoPiece if c is not a piece letter

std::string moveToString(Move m);

// Returns the legal move in pos matching text, or a null Move if there is none.
Move parseMove(Position& pos, const std::string& text);

}  // namespace rs
