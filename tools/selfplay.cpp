// selfplay: random-vs-random games as a stress test of the rules code.
//
// Usage:
//   selfplay [--games N] [--seed S] [--check-all] [--dump-positions FILE]
//
// After every ply it checks that incremental state (hash key, king squares, clocks)
// matches a from-scratch recomputation, that the position survives a text round
// trip, and that the repetition count matches an independent count. With
// --check-all it also makes/unmakes every legal move to confirm the position is
// restored exactly, and checks that noisy-move generation equals the captures and
// promotions among all legal moves. --dump-positions writes one position per line
// (sampled every 7 plies) for cross-checking against another move generator.
//
// Exit code 0 when every check passed, 1 otherwise.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>

#include "raumschach/movegen.h"
#include "raumschach/notation.h"
#include "raumschach/position.h"
#include "raumschach/rng.h"
#include "raumschach/rules.h"

namespace {

int failures = 0;

void fail(const std::string& what, const rs::Position& pos) {
    if (++failures <= 10) std::cerr << "CHECK FAILED: " << what << "\n" << pos.diagram() << pos.toText() << "\n";
}

// Board + side to move only (drops the clocks), for repetition counting.
std::string placementKey(const rs::Position& pos) {
    const std::string text = pos.toText();
    return text.substr(0, text.find(' ') + 2);
}

void checkPly(rs::Position& pos, bool checkAll, int brutalRepetitions) {
    std::string why;
    if (!pos.checkConsistency(&why)) fail("consistency: " + why, pos);

    rs::Position copy;
    if (!copy.setFromText(pos.toText(), &why)) {
        fail("text round trip rejected: " + why, pos);
    } else if (copy.toText() != pos.toText() || copy.key() != pos.key()) {
        fail("text round trip changed the position", pos);
    }

    if (pos.repetitionCount() != brutalRepetitions)
        fail("repetition count " + std::to_string(pos.repetitionCount()) + " != brute force " +
                 std::to_string(brutalRepetitions),
             pos);

    if (!checkAll) return;

    rs::MoveList all, noisy;
    rs::generateLegalMoves(pos, all);
    rs::generateLegalMoves(pos, noisy, rs::GenType::Noisy);

    const rs::Position snapshot = pos;
    std::size_t expectedNoisy = 0;
    for (rs::Move m : all) {
        const bool isNoisy = pos.pieceAt(m.to()) != rs::NoPiece || m.promotion() != rs::NoPieceType;
        if (isNoisy) {
            ++expectedNoisy;
            if (!noisy.contains(m)) fail("noisy generator missed " + rs::moveToString(m), pos);
        }
        pos.makeMove(m);
        if (!pos.checkConsistency(&why)) fail("after " + rs::moveToString(m) + ": " + why, pos);
        pos.unmakeMove();
        if (!pos.samePlacementAndState(snapshot)) fail("make/unmake did not restore after " + rs::moveToString(m), pos);
    }
    if (noisy.size() != expectedNoisy) fail("noisy generator produced extra moves", pos);
}

}  // namespace

int main(int argc, char** argv) {
    int games = 100;
    std::uint64_t seed = 1;
    bool checkAll = false;
    std::string dumpFile;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--games" && i + 1 < argc) {
            games = std::atoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--check-all") {
            checkAll = true;
        } else if (arg == "--dump-positions" && i + 1 < argc) {
            dumpFile = argv[++i];
        } else {
            std::cerr << "usage: selfplay [--games N] [--seed S] [--check-all] [--dump-positions FILE]\n";
            return 2;
        }
    }

    std::ofstream dump;
    if (!dumpFile.empty()) dump.open(dumpFile);

    rs::Rng rng(seed);
    std::map<std::string, int> outcomes;
    std::uint64_t totalPlies = 0;
    int shortest = 1 << 30, longest = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int g = 0; g < games; ++g) {
        rs::Position pos;
        std::unordered_map<std::string, int> seen;  // independent repetition count
        int plies = 0;
        rs::GameStatus status;
        for (;;) {
            const int reps = ++seen[placementKey(pos)];
            checkPly(pos, checkAll, reps);
            if (dump.is_open() && plies % 7 == 3) dump << pos.toText() << "\n";

            status = rs::gameStatus(pos);
            if (status.isOver()) break;

            rs::MoveList moves;
            rs::generateLegalMoves(pos, moves);
            pos.makeMove(moves[static_cast<std::size_t>(rng.below(moves.size()))]);
            ++plies;
        }
        ++outcomes[std::string(rs::toString(status.result)) + " by " + rs::toString(status.reason)];
        totalPlies += static_cast<std::uint64_t>(plies);
        shortest = std::min(shortest, plies);
        longest = std::max(longest, plies);
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << games << " games, seed " << seed << (checkAll ? ", full checks" : "") << ", " << seconds << "s\n";
    std::cout << "plies per game: mean " << (games ? double(totalPlies) / games : 0.0) << ", min " << shortest
              << ", max " << longest << "\n";
    for (const auto& [outcome, count] : outcomes) std::cout << "  " << outcome << ": " << count << "\n";
    std::cout << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string(failures) + " CHECK FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}
