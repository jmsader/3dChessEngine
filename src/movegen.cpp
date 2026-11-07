#include "raumschach/movegen.h"

#include "raumschach/config.h"
#include "raumschach/geometry.h"
#include "raumschach/position.h"

namespace rs {

namespace {

constexpr PieceType kPromotionTypes[] = {Queen, Rook, Bishop, Knight, Unicorn};
constexpr int kNumPromotionTypes = config::kQueenOnlyPromotion ? 1 : 5;

// Adds a pawn move, expanding it into one move per promotion choice when needed.
void addPawnMove(Color us, Square from, Square to, MoveList& out) {
    if (isPromotionSquare(us, to)) {
        for (int i = 0; i < kNumPromotionTypes; ++i) out.push(Move(from, to, kPromotionTypes[i]));
    } else {
        out.push(Move(from, to));
    }
}

void generatePawnMoves(const Position& pos, Square from, Color us, MoveList& out, GenType type) {
    const Tables& t = tables();
    const auto uf = static_cast<std::size_t>(from);

    for (int k = 0; k < 2; ++k) {  // forward push, vertical push
        const Square to = t.pawnPush[us][uf][static_cast<std::size_t>(k)];
        if (to == kNoSquare || pos.pieceAt(to) != NoPiece) continue;
        // A quiet push is only "noisy" when it promotes.
        if (type == GenType::Noisy && !isPromotionSquare(us, to)) continue;
        addPawnMove(us, from, to, out);
    }
    for (Square to : t.pawnCapture[us][uf]) {
        const Piece target = pos.pieceAt(to);
        if (target != NoPiece && colorOf(target) != us) addPawnMove(us, from, to, out);
    }
}

}  // namespace

void generatePseudoLegalMoves(const Position& pos, MoveList& out, GenType type) {
    const Tables& t = tables();
    const Color us = pos.sideToMove();
    const bool quietsWanted = (type == GenType::All);

    // Adds from->to if the target is empty (and quiets are wanted) or an enemy piece.
    auto tryTarget = [&](Square from, Square to) {
        const Piece target = pos.pieceAt(to);
        if (target == NoPiece) {
            if (quietsWanted) out.push(Move(from, to));
        } else if (colorOf(target) != us) {
            out.push(Move(from, to));
        }
    };

    for (Square from = 0; from < kNumSquares; ++from) {
        const Piece p = pos.pieceAt(from);
        if (p == NoPiece || colorOf(p) != us) continue;
        const auto uf = static_cast<std::size_t>(from);
        const PieceType pt = typeOf(p);

        switch (pt) {
            case Pawn:
                generatePawnMoves(pos, from, us, out, type);
                break;
            case Knight:
                for (Square to : t.knight[uf]) tryTarget(from, to);
                break;
            case King:
                for (Square to : t.king[uf]) tryTarget(from, to);
                break;
            default: {  // sliders
                const DirRange range = sliderDirections(pt);
                for (int d = range.begin; d < range.end; ++d) {
                    for (Square to : t.ray[static_cast<std::size_t>(d)][uf]) {
                        tryTarget(from, to);
                        if (pos.pieceAt(to) != NoPiece) break;  // blocked after first piece
                    }
                }
                break;
            }
        }
    }
}

void generateLegalMoves(Position& pos, MoveList& out, GenType type) {
    MoveList pseudo;
    generatePseudoLegalMoves(pos, pseudo, type);
    for (Move m : pseudo)
        if (pos.isLegal(m)) out.push(m);
}

std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) return 1;
    MoveList moves;
    generateLegalMoves(pos, moves);
    if (depth == 1) return moves.size();
    std::uint64_t nodes = 0;
    for (Move m : moves) {
        pos.makeMove(m);
        nodes += perft(pos, depth - 1);
        pos.unmakeMove();
    }
    return nodes;
}

}  // namespace rs
