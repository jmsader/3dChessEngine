// Unit tests for the rules code. No framework dependency: CHECK / CHECK_EQ record
// failures and the process exits non-zero if any failed.
//
// These tests were written by the same author as the engine, so they catch
// implementation slips but cannot catch a shared misreading of the rules. The
// independent reference generator and hand-checked positions (see README) cover that.
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "raumschach/config.h"
#include "raumschach/geometry.h"
#include "raumschach/movegen.h"
#include "raumschach/notation.h"
#include "raumschach/position.h"
#include "raumschach/rules.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failures;                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n";  \
        }                                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        ++g_checks;                                                                        \
        const auto a_ = (actual);                                                          \
        const auto e_ = (expected);                                                        \
        if (!(a_ == e_)) {                                                                 \
            ++g_failures;                                                                  \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #actual ", " #expected \
                      << ") failed: got " << a_ << ", expected " << e_ << "\n";            \
        }                                                                                  \
    } while (0)

using namespace rs;

Position load(const std::string& text) {
    Position p;
    std::string error;
    if (!p.setFromText(text, &error)) {
        ++g_failures;
        std::cerr << "test position rejected: " << error << "\n  " << text << "\n";
    }
    return p;
}

Square sq(const char* name) {
    const Square s = parseSquare(name);
    if (s == kNoSquare) {
        ++g_failures;
        std::cerr << "bad square name in test: " << name << "\n";
        return 0;
    }
    return s;
}

// Destinations (as names) of legal moves from one square, promotions collapsed.
std::set<std::string> destinationsFrom(Position& pos, const char* from) {
    MoveList moves;
    generateLegalMoves(pos, moves);
    std::set<std::string> out;
    for (Move m : moves)
        if (m.from() == sq(from)) out.insert(squareName(m.to()));
    return out;
}

int countMovesFrom(Position& pos, const char* from) {
    MoveList moves;
    generateLegalMoves(pos, moves);
    int n = 0;
    for (Move m : moves)
        if (m.from() == sq(from)) ++n;
    return n;
}

const int kPromoChoices = config::kQueenOnlyPromotion ? 1 : 5;

// ------------------------------------------------------------------ geometry

void testGeometryTables() {
    const Tables& t = tables();
    auto sliderCount = [&](Square s, PieceType pt) {
        int n = 0;
        const DirRange r = sliderDirections(pt);
        for (int d = r.begin; d < r.end; ++d) n += t.ray[static_cast<std::size_t>(d)][static_cast<std::size_t>(s)].count;
        return n;
    };
    const Square center = sq("Cc3");
    CHECK_EQ(sliderCount(center, Rook), 12);
    CHECK_EQ(sliderCount(center, Bishop), 24);
    CHECK_EQ(sliderCount(center, Unicorn), 16);
    CHECK_EQ(sliderCount(center, Queen), 52);
    CHECK_EQ(static_cast<int>(t.king[static_cast<std::size_t>(center)].count), 26);
    CHECK_EQ(static_cast<int>(t.knight[static_cast<std::size_t>(center)].count), 24);

    const Square corner = sq("Aa1");
    CHECK_EQ(sliderCount(corner, Rook), 12);
    CHECK_EQ(sliderCount(corner, Bishop), 12);
    CHECK_EQ(sliderCount(corner, Unicorn), 4);
    CHECK_EQ(sliderCount(corner, Queen), 28);
    CHECK_EQ(static_cast<int>(t.king[static_cast<std::size_t>(corner)].count), 7);
    CHECK_EQ(static_cast<int>(t.knight[static_cast<std::size_t>(corner)].count), 6);

    // Every square name round-trips.
    for (Square s = 0; s < kNumSquares; ++s) CHECK_EQ(parseSquare(squareName(s)), s);
    CHECK_EQ(squareName(sq("Ac2")), std::string("Ac2"));
    CHECK_EQ(levelOf(sq("Bd4")), 1);
    CHECK_EQ(fileOf(sq("Bd4")), 3);
    CHECK_EQ(rankOf(sq("Bd4")), 3);
    CHECK_EQ(parseSquare("Fa1"), kNoSquare);
    CHECK_EQ(parseSquare("Af1"), kNoSquare);
    CHECK_EQ(parseSquare("Aa6"), kNoSquare);
}

// ------------------------------------------------------------------ pieces on a nearly empty board

void testPieceMobilityFromCenter() {
    // Kings sit at offsets (-2,-1,-1) and (2,1,1) from Cc3: on no queen line through
    // Cc3 and not a knight jump away, so they neither block nor get attacked.
    struct Case {
        char piece;
        int expected;
    } cases[] = {{'R', 12}, {'B', 24}, {'U', 16}, {'Q', 52}, {'N', 24}};
    for (const Case& c : cases) {
        // White king Ba2, Black king De4, piece on Cc3.
        std::string text = "5/5/5/5/5|5/K4/5/5/5|5/5/2";
        text += c.piece;
        text += "2/5/5|5/5/5/4k/5|5/5/5/5/5 w 0 1";
        Position p = load(text);
        CHECK_EQ(countMovesFrom(p, "Cc3"), c.expected);
    }
    // King on Cc3; Black king on Ae1 attacks exactly one of its 26 neighbours (Bd2).
    Position k = load("4k/5/5/5/5|5/5/5/5/5|5/5/2K2/5/5|5/5/5/5/5|5/5/5/5/5 w 0 1");
    CHECK_EQ(countMovesFrom(k, "Cc3"), 25);
    CHECK(destinationsFrom(k, "Cc3").count("Bd2") == 0);
}

void testSlidersBlockedAndCapture() {
    // White rook Cc3; own pawn on Cc4 blocks +y; black knight on Ce3 can be captured, Cd3 empty.
    Position p = load("K4/5/5/5/5|5/5/5/5/5|5/5/2R1n/2P2/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    const std::set<std::string> d = destinationsFrom(p, "Cc3");
    CHECK(d.count("Cd3") == 1);
    CHECK(d.count("Ce3") == 1);   // capture
    CHECK(d.count("Cc4") == 0);   // own piece
    CHECK(d.count("Cc5") == 0);   // behind own piece
    CHECK_EQ(static_cast<int>(d.size()), 12 - 2);
}

void testKnightJumps() {
    // Knight on Ab1 surrounded by pieces still reaches its targets (start position).
    Position p;
    const std::set<std::string> d = destinationsFrom(p, "Ab1");
    const std::set<std::string> expected = {"Aa3", "Ac3", "Bb3", "Ca1", "Cc1", "Cb2"};
    CHECK(d == expected);
}

// ------------------------------------------------------------------ pawns

void testWhitePawnMovesMatchRulesExample() {
    // Rules example: a White pawn on Ac2 moves to Ac3 or Bc2 and captures on Ab3, Ad3,
    // Bb2 or Bd2 -- but NOT on Bc3 (the disputed forward-up diagonal).
    Position p = load("5/2P2/1p1p1/5/5|5/1p1p1/2p2/5/5|5/5/5/5/5|5/5/5/5/5|K4/5/5/5/4k w 0 1");
    const std::set<std::string> expected = {"Ac3", "Bc2", "Ab3", "Ad3", "Bb2", "Bd2"};
    CHECK(destinationsFrom(p, "Ac2") == expected);
}

void testBlackPawnMovesMirror() {
    // Mirror image: Black pawn on Ec4 moves to Ec3 or Dc4, captures Eb3, Ed3, Db4, Dd4, not Dc3.
    Position p = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/2P2/1P1P1/5|5/5/1P1P1/2p2/4k b 0 1");
    const std::set<std::string> expected = {"Ec3", "Dc4", "Eb3", "Ed3", "Db4", "Dd4"};
    CHECK(destinationsFrom(p, "Ec4") == expected);
}

void testPawnBlockedAndNoSidewaysOrBackward() {
    // Pawn on Bc2 with enemy pieces directly in front (Bc3) and above (Cc2): no pushes,
    // and a pawn never captures straight ahead. Own piece on a capture square (Bb3)
    // cannot be captured; empty capture squares (Cb2, Cd2) give nothing.
    Position p = load("K4/5/5/5/5|5/2P2/1Nnp1/5/5|5/2n2/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    const std::set<std::string> expected = {"Bd3"};  // only the enemy pawn on Bd3
    CHECK(destinationsFrom(p, "Bc2") == expected);
}

void testNoDoubleStep() {
    Position p;
    const std::set<std::string> d = destinationsFrom(p, "Bc2");
    const std::set<std::string> expected = {"Bc3", "Cc2"};
    CHECK(d == expected);
}

void testPromotion() {
    // White pawn on Ec4: forward to Ec5 promotes (level E, rank 5).
    Position a = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/2P2/k4 w 0 1");
    CHECK_EQ(countMovesFrom(a, "Ec4"), kPromoChoices);
    // White pawn on Dc5: upward to Ec5 promotes.
    Position b = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/2P2|k4/5/5/5/5 w 0 1");
    CHECK_EQ(countMovesFrom(b, "Dc5"), kPromoChoices);
    // White pawn on Cc5: upward to Dc5 is NOT a promotion (only level E, rank 5 promotes).
    Position c = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/2P2|5/5/5/5/5|k4/5/5/5/5 w 0 1");
    CHECK_EQ(countMovesFrom(c, "Cc5"), 1);
    // Capture-promotion: pawn on Dc5 capturing on Eb5 / Ed5 promotes too.
    Position d = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/2P2|k4/5/5/5/1n1n1 w 0 1");
    CHECK_EQ(countMovesFrom(d, "Dc5"), 3 * kPromoChoices);
    // Black: pawn on Bc1 moving down to Ac1 promotes (level A, rank 1).
    Position e = load("5/5/5/5/4K|2p2/5/5/5/5|5/5/5/5/5|5/5/5/5/5|k4/5/5/5/5 b 0 1");
    CHECK_EQ(countMovesFrom(e, "Bc1"), kPromoChoices);

    // Make/unmake of a promotion restores the pawn.
    MoveList moves;
    generateLegalMoves(a, moves);
    const Position before = a;
    for (Move m : moves) {
        if (m.from() != sq("Ec4")) continue;
        CHECK(m.promotion() != NoPieceType);
        a.makeMove(m);
        CHECK_EQ(static_cast<int>(a.pieceAt(sq("Ec5"))), static_cast<int>(makePiece(White, m.promotion())));
        CHECK(a.checkConsistency());
        a.unmakeMove();
        CHECK(a.samePlacementAndState(before));
    }
    CHECK_EQ(static_cast<int>(a.pieceAt(sq("Ec4"))), static_cast<int>(WPawn));
}

// ------------------------------------------------------------------ check, pins, game end

void testPinnedPieceCannotMove() {
    // White king Aa1, White knight Ac1, Black rook Ae1: the knight is pinned along rank 1.
    Position p = load("K1N1r/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    CHECK_EQ(countMovesFrom(p, "Ac1"), 0);
}

void testKingCannotMoveIntoCheck() {
    // White king Aa1, Black rook on Bb5 attacks down the b-file of level B (Bb4..Bb1),
    // so the king's neighbours Bb1 and Bb2 are forbidden.
    Position p = load("K4/5/5/5/5|5/5/5/5/1r3|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    for (const std::string& to : destinationsFrom(p, "Aa1")) {
        Position copy = p;
        Move m = parseMove(copy, "Aa1" + to);
        CHECK(!m.isNull());
        copy.makeMove(m);
        CHECK(!copy.isAttacked(sq(to.c_str()), Black));
    }
    const std::set<std::string> kingMoves = destinationsFrom(p, "Aa1");
    CHECK(kingMoves.count("Bb1") == 0);
    CHECK(kingMoves.count("Bb2") == 0);
    CHECK_EQ(static_cast<int>(kingMoves.size()), 5);  // 7 neighbours of a corner minus 2
}

void testCheckmate() {
    // Black king Aa1, White queen Bb2 (defended by White king Cc3). Black to move is mated.
    Position p = load("k4/5/5/5/5|5/1Q3/5/5/5|5/5/2K2/5/5|5/5/5/5/5|5/5/5/5/5 b 0 1");
    CHECK(p.inCheck());
    const GameStatus s = gameStatus(p);
    CHECK(s.result == GameResult::WhiteWins);
    CHECK(s.reason == EndReason::Checkmate);
}

void testStalemate() {
    // Black king Aa1 not in check, every neighbour covered by White king Ac1 / queen Ba3.
    Position p = load("k1K2/5/5/5/5|5/5/Q4/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5 b 0 1");
    CHECK(!p.inCheck());
    const GameStatus s = gameStatus(p);
    CHECK(s.result == GameResult::Draw);
    CHECK(s.reason == EndReason::Stalemate);
}

void testThreefoldRepetition() {
    Position p;
    const char* cycle[] = {"Ab1Ca1", "Eb5Ca5", "Ca1Ab1", "Ca5Eb5"};
    for (int round = 0; round < 2; ++round) {
        for (const char* text : cycle) {
            const Move m = parseMove(p, text);
            CHECK(!m.isNull());
            p.makeMove(m);
        }
        if (round == 0) {
            CHECK_EQ(p.repetitionCount(), 2);
            CHECK(!gameStatus(p).isOver());
        }
    }
    CHECK_EQ(p.repetitionCount(), 3);
    const GameStatus s = gameStatus(p);
    CHECK(s.result == GameResult::Draw);
    CHECK(s.reason == EndReason::Repetition);

    // An irreversible move (pawn push) breaks the chain.
    const Move push = parseMove(p, "Ba2Ba3");
    CHECK(!push.isNull());
    p.makeMove(push);
    CHECK_EQ(p.repetitionCount(), 1);
    CHECK_EQ(p.halfmoveClock(), 0);
}

void testFiftyMoveRule() {
    // Halfmove clock 99, a quiet knight move makes it 100: draw.
    Position p = load("K4/5/5/5/5|5/5/5/5/5|5/5/2N2/5/5|5/5/5/5/5|5/5/5/5/4k w 99 80");
    CHECK(!gameStatus(p).isOver());
    Move m = parseMove(p, "Cc3Ca2");
    CHECK(!m.isNull());
    p.makeMove(m);
    CHECK_EQ(p.halfmoveClock(), 100);
    GameStatus s = gameStatus(p);
    CHECK(s.result == GameResult::Draw);
    CHECK(s.reason == EndReason::FiftyMoveRule);

    // Checkmate delivered on the 100th half-move is still checkmate.
    Position q = load("k4/5/5/5/5|5/5/5/1Q3/5|5/5/2K2/5/5|5/5/5/5/5|5/5/5/5/5 w 99 80");
    m = parseMove(q, "Bb4Bb2");
    CHECK(!m.isNull());
    q.makeMove(m);
    s = gameStatus(q);
    CHECK(s.result == GameResult::WhiteWins);
    CHECK(s.reason == EndReason::Checkmate);

    // Captures reset the clock.
    Position r = load("K4/5/5/5/5|5/5/5/5/5|5/5/2N2/n4/5|5/5/5/5/5|5/5/5/5/4k w 57 80");
    m = parseMove(r, "Cc3Ca4");
    CHECK(!m.isNull());
    r.makeMove(m);
    CHECK_EQ(r.halfmoveClock(), 0);
}

void testBareKings() {
    Position kk = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    GameStatus s = gameStatus(kk);
    CHECK(s.result == GameResult::Draw);
    CHECK(s.reason == EndReason::BareKings);

    // King and knight vs king is NOT drawn by this rule.
    Position kn = load("KN3/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    CHECK(!gameStatus(kn).isOver());

    // Capturing the last other piece ends the game.
    Position cap = load("K4/n4/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    const Move m = parseMove(cap, "Aa1Aa2");
    CHECK(!m.isNull());
    cap.makeMove(m);
    s = gameStatus(cap);
    CHECK(s.reason == EndReason::BareKings);
}

void testStartPositionIsPointSymmetric() {
    // Black's army is White's reflected through the centre of the cube:
    // a piece on (x,y,z) has an opposite-coloured twin on (4-x,4-y,4-z).
    Position p;
    int pieces = 0;
    for (Square s = 0; s < kNumSquares; ++s) {
        const Piece piece = p.pieceAt(s);
        if (piece == NoPiece) continue;
        ++pieces;
        const Square mirror = makeSquare(4 - fileOf(s), 4 - rankOf(s), 4 - levelOf(s));
        CHECK_EQ(static_cast<int>(p.pieceAt(mirror)),
                 static_cast<int>(makePiece(~colorOf(piece), typeOf(piece))));
    }
    CHECK_EQ(pieces, 40);
    // Spot checks from RULES.md.
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Da5"))), static_cast<int>(BUnicorn));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Db5"))), static_cast<int>(BBishop));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Dc5"))), static_cast<int>(BQueen));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Dd5"))), static_cast<int>(BUnicorn));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("De5"))), static_cast<int>(BBishop));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Bb1"))), static_cast<int>(WUnicorn));
    CHECK_EQ(static_cast<int>(p.pieceAt(sq("Ec5"))), static_cast<int>(BKing));

    // By symmetry Black has as many replies to a quiet first move as White had moves.
    MoveList black;
    Position afterNull = load(std::string(Position::kStartText).replace(
        std::string(Position::kStartText).find(" w "), 3, " b "));
    generateLegalMoves(afterNull, black);
    CHECK_EQ(static_cast<int>(black.size()), 61);
}

// ------------------------------------------------------------------ text format, hashing, notation

void testTextRoundTripAndValidation() {
    Position start;
    CHECK_EQ(start.toText(), std::string(Position::kStartText));
    CHECK(start.checkConsistency());

    Position p;
    std::string error;
    const char* bad[] = {
        "5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5 w 0 1",                    // 4 levels
        "K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/K3k w 0 1",        // two white kings
        "K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5 w 0 1",          // no black king
        "K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/P3k w 0 1",        // pawn on E*5
        "K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k x 0 1",         // bad side
        "K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w -1 1",        // bad clock
        "K3/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1",         // short rank
        "K5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1",         // long rank
        "K3X/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1",        // bad piece letter
        "K3r/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k b 0 1",        // White (not to move) in check
    };
    for (const char* text : bad) {
        const std::string beforeText = p.toText();
        CHECK(!p.setFromText(text, &error));
        CHECK_EQ(p.toText(), beforeText);  // unchanged on failure
    }

    // Optional counters default to 0 and 1.
    CHECK(p.setFromText("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k b"));
    CHECK_EQ(p.halfmoveClock(), 0);
    CHECK_EQ(p.fullmoveNumber(), 1);
}

void testIncrementalKeyMatchesFreshPosition() {
    Position p;
    for (const char* text : {"Bc2Cc2", "Dc4Cc4", "Ab1Ac3", "Ea4Ea3"}) {
        const Move m = parseMove(p, text);
        CHECK(!m.isNull());
        p.makeMove(m);
        Position fresh = load(p.toText());
        CHECK_EQ(p.key(), fresh.key());
        CHECK_EQ(p.key(), p.computeKey());
    }
    // Same placement, different side to move -> different key.
    Position w = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k w 0 1");
    Position b = load("K4/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/5|5/5/5/5/4k b 0 1");
    CHECK(w.key() != b.key());
}

void testMoveNotation() {
    Position p;
    MoveList moves;
    generateLegalMoves(p, moves);
    for (Move m : moves) CHECK(parseMove(p, moveToString(m)) == m);
    CHECK(parseMove(p, "Aa1Aa2").isNull());  // own piece in the way
    CHECK(parseMove(p, "nonsense").isNull());
    CHECK_EQ(moveToString(Move(sq("Ec4"), sq("Ec5"), Unicorn)), std::string("Ec4Ec5u"));
}

void testNoisyGeneration() {
    // From the Ac2 pawn example: noisy moves are exactly the four pawn captures.
    Position p = load("5/2P2/1p1p1/5/5|5/1p1p1/2p2/5/5|5/5/5/5/5|5/5/5/5/5|K4/5/5/5/4k w 0 1");
    MoveList noisy;
    generateLegalMoves(p, noisy, GenType::Noisy);
    std::set<std::string> got;
    for (Move m : noisy) got.insert(moveToString(m));
    const std::set<std::string> expected = {"Ac2Ab3", "Ac2Ad3", "Ac2Bb2", "Ac2Bd2"};
    CHECK(got == expected);
}

// ------------------------------------------------------------------ perft regression

void testPerftRegression() {
    // Hand-verified: 61 legal moves from the start (pawns 15, knights 12, queen 14,
    // bishops 13, unicorns 7; rooks and king blocked). Deeper values are this
    // generator's own output -- regression guards only, until an independent
    // reference generator confirms them.
    Position p;
    CHECK_EQ(perft(p, 1), 61u);
    CHECK_EQ(perft(p, 2), 3608u);
    CHECK_EQ(perft(p, 3), 236511u);
    CHECK(p.samePlacementAndState(Position()));
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"geometry tables", testGeometryTables},
        {"piece mobility from center", testPieceMobilityFromCenter},
        {"sliders blocked and capture", testSlidersBlockedAndCapture},
        {"knight jumps", testKnightJumps},
        {"white pawn (rules example)", testWhitePawnMovesMatchRulesExample},
        {"black pawn mirror", testBlackPawnMovesMirror},
        {"pawn blocked", testPawnBlockedAndNoSidewaysOrBackward},
        {"no double step", testNoDoubleStep},
        {"promotion", testPromotion},
        {"pinned piece", testPinnedPieceCannotMove},
        {"king cannot move into check", testKingCannotMoveIntoCheck},
        {"checkmate", testCheckmate},
        {"stalemate", testStalemate},
        {"threefold repetition", testThreefoldRepetition},
        {"fifty-move rule", testFiftyMoveRule},
        {"bare kings", testBareKings},
        {"start position symmetry", testStartPositionIsPointSymmetric},
        {"text round trip and validation", testTextRoundTripAndValidation},
        {"incremental key", testIncrementalKeyMatchesFreshPosition},
        {"move notation", testMoveNotation},
        {"noisy generation", testNoisyGeneration},
        {"perft regression", testPerftRegression},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::cout << (g_failures == before ? "  ok    " : "  FAIL  ") << name << "\n";
    }
    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
