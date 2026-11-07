#include "raumschach/position.h"

#include <cctype>
#include <sstream>

#include "raumschach/geometry.h"
#include "raumschach/notation.h"
#include "raumschach/zobrist.h"

namespace rs {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : text) {
        if (ch == sep) {
            parts.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    parts.push_back(current);
    return parts;
}

bool parseInt(const std::string& text, int& out) {
    if (text.empty() || text.size() > 9) return false;
    int value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

}  // namespace

Position::Position() : Position(EmptyTag{}) {
    const bool ok = setFromText(kStartText);
    assert(ok);
    (void)ok;
}

void Position::clear() {
    board_.fill(NoPiece);
    sideToMove_ = White;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
    key_ = 0;
    kingSquare_ = {kNoSquare, kNoSquare};
    history_.clear();
}

void Position::put(Square s, Piece p) {
    const auto us = static_cast<std::size_t>(s);
    key_ ^= zobrist().pieceSquare[board_[us]][us];
    board_[us] = p;
    key_ ^= zobrist().pieceSquare[p][us];
    if (p != NoPiece && typeOf(p) == King) kingSquare_[colorOf(p)] = s;
}

// ------------------------------------------------------------------ text I/O

bool Position::setFromText(const std::string& text, std::string* error) {
    std::istringstream in(text);
    std::string boardField, sideField, halfField = "0", fullField = "1";
    if (!(in >> boardField >> sideField)) {
        setError(error, "expected at least a board field and a side-to-move field");
        return false;
    }
    in >> halfField >> fullField;  // optional
    std::string extra;
    if (in >> extra) {
        setError(error, "unexpected trailing text: " + extra);
        return false;
    }

    // Build into a scratch position so *this is untouched if parsing fails.
    Position p{EmptyTag{}};

    const std::vector<std::string> levels = split(boardField, '|');
    if (levels.size() != kSize) {
        setError(error, "board must have 5 levels separated by '|'");
        return false;
    }
    int kingCount[kNumColors] = {0, 0};
    for (int z = 0; z < kSize; ++z) {
        const std::vector<std::string> ranks = split(levels[static_cast<std::size_t>(z)], '/');
        if (ranks.size() != kSize) {
            setError(error, std::string("level ") + char('A' + z) + " must have 5 ranks separated by '/'");
            return false;
        }
        for (int y = 0; y < kSize; ++y) {
            int x = 0;
            for (char ch : ranks[static_cast<std::size_t>(y)]) {
                if (ch >= '1' && ch <= '5') {
                    x += ch - '0';
                } else {
                    const Piece piece = charToPiece(ch);
                    if (piece == NoPiece || x >= kSize) {
                        setError(error, std::string("bad character or too many cells in level ") +
                                            char('A' + z) + " rank " + char('1' + y));
                        return false;
                    }
                    const Square s = makeSquare(x, y, z);
                    if (typeOf(piece) == King) ++kingCount[colorOf(piece)];
                    if (typeOf(piece) == Pawn && isPromotionSquare(colorOf(piece), s)) {
                        setError(error, "pawn on its own promotion square: " + squareName(s));
                        return false;
                    }
                    p.put(s, piece);
                    ++x;
                }
                if (x > kSize) break;
            }
            if (x != kSize) {
                setError(error, std::string("level ") + char('A' + z) + " rank " + char('1' + y) +
                                    " does not describe exactly 5 cells");
                return false;
            }
        }
    }
    if (kingCount[White] != 1 || kingCount[Black] != 1) {
        setError(error, "each side must have exactly one king");
        return false;
    }

    if (sideField == "w") {
        p.sideToMove_ = White;
    } else if (sideField == "b") {
        p.sideToMove_ = Black;
        p.key_ ^= zobrist().blackToMove;
    } else {
        setError(error, "side to move must be 'w' or 'b'");
        return false;
    }

    if (!parseInt(halfField, p.halfmoveClock_) || !parseInt(fullField, p.fullmoveNumber_) ||
        p.fullmoveNumber_ < 1) {
        setError(error, "halfmove clock must be >= 0 and fullmove number >= 1");
        return false;
    }

    if (p.isAttacked(p.kingSquare_[~p.sideToMove_], p.sideToMove_)) {
        setError(error, "the side not to move is in check (illegal position)");
        return false;
    }

    *this = std::move(p);
    return true;
}

std::string Position::toText() const {
    std::string out;
    for (int z = 0; z < kSize; ++z) {
        if (z > 0) out += '|';
        for (int y = 0; y < kSize; ++y) {
            if (y > 0) out += '/';
            int empty = 0;
            for (int x = 0; x < kSize; ++x) {
                const Piece p = pieceAt(makeSquare(x, y, z));
                if (p == NoPiece) {
                    ++empty;
                    continue;
                }
                if (empty > 0) out += char('0' + empty);
                empty = 0;
                out += pieceToChar(p);
            }
            if (empty > 0) out += char('0' + empty);
        }
    }
    out += sideToMove_ == White ? " w " : " b ";
    out += std::to_string(halfmoveClock_) + " " + std::to_string(fullmoveNumber_);
    return out;
}

std::string Position::diagram() const {
    std::ostringstream out;
    out << "   ";
    for (int z = 0; z < kSize; ++z) out << "Level " << char('A' + z) << "     ";
    out << "\n";
    for (int y = kSize - 1; y >= 0; --y) {
        out << ' ' << (y + 1) << ' ';
        for (int z = 0; z < kSize; ++z) {
            for (int x = 0; x < kSize; ++x) out << pieceToChar(pieceAt(makeSquare(x, y, z))) << ' ';
            out << "  ";
        }
        out << "\n";
    }
    out << "   ";
    for (int z = 0; z < kSize; ++z) out << "a b c d e   ";
    out << "\n" << (sideToMove_ == White ? "White" : "Black") << " to move, halfmove clock "
        << halfmoveClock_ << ", move " << fullmoveNumber_ << "\n";
    return out.str();
}

// ------------------------------------------------------------------ attacks

bool Position::isAttacked(Square s, Color by) const {
    const Tables& t = tables();
    const auto us = static_cast<std::size_t>(s);

    for (Square from : t.pawnAttackers[by][us])
        if (board_[static_cast<std::size_t>(from)] == makePiece(by, Pawn)) return true;
    for (Square from : t.knight[us])
        if (board_[static_cast<std::size_t>(from)] == makePiece(by, Knight)) return true;
    for (Square from : t.king[us])
        if (board_[static_cast<std::size_t>(from)] == makePiece(by, King)) return true;

    const Piece queen = makePiece(by, Queen);
    for (int d = 0; d < kNumDirections; ++d) {
        // The slider (other than the queen) that moves along this direction.
        const Piece slider = makePiece(by, d < kRookDirEnd     ? Rook
                                           : d < kBishopDirEnd ? Bishop
                                                               : Unicorn);
        for (Square cur : t.ray[static_cast<std::size_t>(d)][us]) {
            const Piece p = board_[static_cast<std::size_t>(cur)];
            if (p == NoPiece) continue;
            if (p == queen || p == slider) return true;
            break;  // blocked
        }
    }
    return false;
}

// ------------------------------------------------------------------ make / unmake

void Position::makeMove(Move m) {
    const ZobristKeys& z = zobrist();
    const Square from = m.from(), to = m.to();
    const auto uf = static_cast<std::size_t>(from), ut = static_cast<std::size_t>(to);
    const Piece moved = board_[uf];
    const Piece captured = board_[ut];
    assert(moved != NoPiece && colorOf(moved) == sideToMove_);
    assert(captured == NoPiece || (colorOf(captured) != sideToMove_ && typeOf(captured) != King));

    history_.push_back(Undo{m, captured, halfmoveClock_, key_});

    const Piece placed = m.promotion() != NoPieceType ? makePiece(sideToMove_, m.promotion()) : moved;
    key_ ^= z.pieceSquare[moved][uf] ^ z.pieceSquare[captured][ut] ^ z.pieceSquare[placed][ut];
    board_[uf] = NoPiece;
    board_[ut] = placed;
    if (typeOf(moved) == King) kingSquare_[sideToMove_] = to;

    halfmoveClock_ = (typeOf(moved) == Pawn || captured != NoPiece) ? 0 : halfmoveClock_ + 1;
    if (sideToMove_ == Black) ++fullmoveNumber_;
    sideToMove_ = ~sideToMove_;
    key_ ^= z.blackToMove;
}

void Position::unmakeMove() {
    assert(!history_.empty());
    const Undo u = history_.back();
    history_.pop_back();

    sideToMove_ = ~sideToMove_;  // back to the side that made the move
    if (sideToMove_ == Black) --fullmoveNumber_;

    const auto uf = static_cast<std::size_t>(u.move.from()), ut = static_cast<std::size_t>(u.move.to());
    const Piece placed = board_[ut];
    const Piece original = u.move.promotion() != NoPieceType ? makePiece(sideToMove_, Pawn) : placed;
    board_[uf] = original;
    board_[ut] = u.captured;
    if (typeOf(original) == King) kingSquare_[sideToMove_] = u.move.from();

    halfmoveClock_ = u.halfmoveClock;
    key_ = u.key;
}

bool Position::isLegal(Move m) {
    const Color us = sideToMove_;
    makeMove(m);
    const bool legal = !isAttacked(kingSquare_[us], ~us);
    unmakeMove();
    return legal;
}

int Position::repetitionCount() const {
    // Positions with the same side to move lie 2, 4, 6, ... plies back. Only the last
    // halfmoveClock_ plies can repeat: anything earlier precedes an irreversible move.
    int count = 1;
    const int n = static_cast<int>(history_.size());
    const int window = halfmoveClock_ < n ? halfmoveClock_ : n;
    for (int back = 2; back <= window; back += 2)
        if (history_[static_cast<std::size_t>(n - back)].key == key_) ++count;
    return count;
}

// ------------------------------------------------------------------ testing support

std::uint64_t Position::computeKey() const {
    std::uint64_t k = 0;
    for (std::size_t s = 0; s < board_.size(); ++s) k ^= zobrist().pieceSquare[board_[s]][s];
    if (sideToMove_ == Black) k ^= zobrist().blackToMove;
    return k;
}

bool Position::checkConsistency(std::string* why) const {
    if (key_ != computeKey()) {
        setError(why, "incremental key differs from recomputed key");
        return false;
    }
    int kings[kNumColors] = {0, 0};
    for (Square s = 0; s < kNumSquares; ++s) {
        const Piece p = pieceAt(s);
        if (p == NoPiece) continue;
        if (typeOf(p) == King) {
            ++kings[colorOf(p)];
            if (kingSquare_[colorOf(p)] != s) {
                setError(why, "king square table out of date");
                return false;
            }
        }
        if (typeOf(p) == Pawn && isPromotionSquare(colorOf(p), s)) {
            setError(why, "unpromoted pawn on promotion square " + squareName(s));
            return false;
        }
    }
    if (kings[White] != 1 || kings[Black] != 1) {
        setError(why, "king count is not one per side");
        return false;
    }
    if (halfmoveClock_ < 0 || fullmoveNumber_ < 1) {
        setError(why, "bad move counters");
        return false;
    }
    if (isAttacked(kingSquare_[~sideToMove_], sideToMove_)) {
        setError(why, "side not to move is in check");
        return false;
    }
    return true;
}

bool Position::samePlacementAndState(const Position& other) const {
    return board_ == other.board_ && sideToMove_ == other.sideToMove_ &&
           halfmoveClock_ == other.halfmoveClock_ && fullmoveNumber_ == other.fullmoveNumber_ &&
           key_ == other.key_ && kingSquare_ == other.kingSquare_;
}

}  // namespace rs
