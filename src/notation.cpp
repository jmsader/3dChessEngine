#include "raumschach/notation.h"

#include <cctype>

#include "raumschach/geometry.h"
#include "raumschach/movegen.h"
#include "raumschach/position.h"

namespace rs {

namespace {
// Indexed by PieceType.
constexpr char kTypeChars[kNumPieceTypes] = {'.', 'P', 'N', 'B', 'R', 'U', 'Q', 'K'};
}  // namespace

char pieceToChar(Piece p) {
    if (p == NoPiece) return '.';
    const char c = kTypeChars[typeOf(p)];
    return colorOf(p) == White ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

Piece charToPiece(char c) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (int t = Pawn; t <= King; ++t) {
        if (kTypeChars[t] == upper) {
            const Color color = (c == upper) ? White : Black;
            return makePiece(color, PieceType(t));
        }
    }
    return NoPiece;
}

std::string moveToString(Move m) {
    if (m.isNull()) return "0000";
    std::string text = squareName(m.from()) + squareName(m.to());
    if (m.promotion() != NoPieceType)
        text += static_cast<char>(std::tolower(static_cast<unsigned char>(kTypeChars[m.promotion()])));
    return text;
}

Move parseMove(Position& pos, const std::string& text) {
    MoveList moves;
    generateLegalMoves(pos, moves);
    for (Move m : moves)
        if (moveToString(m) == text) return m;
    return Move();
}

}  // namespace rs
