// perft: count leaf nodes of the legal move tree, for validating move generation.
//
// Usage:
//   perft <depth> [--text "<position text>"] [--divide] [--stats]
//   perft <depth> --batch < positions.txt
//
//   --text    position to start from (default: the starting position)
//   --divide  print the node count below each root move at the final depth
//             (compare two generators move by move to locate a disagreement)
//   --stats   also count captures, promotions, checks and checkmates at the final depth
//   --batch   read positions from stdin, one per line; print perft(depth) for each
//             (same output format as validation/ref_perft.py, for diffing)
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "raumschach/movegen.h"
#include "raumschach/notation.h"
#include "raumschach/position.h"

namespace {

struct LeafStats {
    std::uint64_t nodes = 0, captures = 0, promotions = 0, checks = 0, checkmates = 0;
};

// Counts properties of the moves leading to each leaf (the last move played).
void perftStats(rs::Position& pos, int depth, LeafStats& stats) {
    rs::MoveList moves;
    rs::generateLegalMoves(pos, moves);
    for (rs::Move m : moves) {
        pos.makeMove(m);
        if (depth == 1) {
            ++stats.nodes;
            if (pos.lastCaptured() != rs::NoPiece) ++stats.captures;
            if (m.promotion() != rs::NoPieceType) ++stats.promotions;
            if (pos.inCheck()) {
                ++stats.checks;
                rs::MoveList replies;
                rs::generateLegalMoves(pos, replies);
                if (replies.empty()) ++stats.checkmates;
            }
        } else {
            perftStats(pos, depth - 1, stats);
        }
        pos.unmakeMove();
    }
}

int usage() {
    std::cerr << "usage: perft <depth> [--text \"<position text>\"] [--divide] [--stats]\n"
                 "       perft <depth> --batch < positions.txt\n";
    return 2;
}

int runBatch(int depth) {
    std::string line;
    int lineNumber = 0;
    while (std::getline(std::cin, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // Windows line endings
        if (line.empty()) continue;
        rs::Position pos;
        std::string error;
        if (!pos.setFromText(line, &error)) {
            std::cerr << "line " << lineNumber << ": invalid position: " << error << "\n";
            return 1;
        }
        std::cout << rs::perft(pos, depth) << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const int depth = std::atoi(argv[1]);
    if (depth < 1) return usage();

    std::string text = rs::Position::kStartText;
    bool divide = false, stats = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--text" && i + 1 < argc) {
            text = argv[++i];
        } else if (arg == "--divide") {
            divide = true;
        } else if (arg == "--stats") {
            stats = true;
        } else if (arg == "--batch") {
            return runBatch(depth);
        } else {
            return usage();
        }
    }

    rs::Position pos;
    std::string error;
    if (!pos.setFromText(text, &error)) {
        std::cerr << "invalid position: " << error << "\n";
        return 1;
    }
    std::cout << pos.diagram() << pos.toText() << "\n\n";

    using Clock = std::chrono::steady_clock;
    for (int d = 1; d <= depth; ++d) {
        const auto start = Clock::now();
        const std::uint64_t nodes = rs::perft(pos, d);
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        std::cout << "depth " << d << "  nodes " << nodes << "  time " << seconds << "s";
        if (seconds > 0) std::cout << "  nps " << static_cast<std::uint64_t>(static_cast<double>(nodes) / seconds);
        std::cout << "\n";
    }

    if (divide) {
        std::cout << "\ndivide at depth " << depth << ":\n";
        rs::MoveList moves;
        rs::generateLegalMoves(pos, moves);
        std::uint64_t total = 0;
        for (rs::Move m : moves) {
            pos.makeMove(m);
            const std::uint64_t n = rs::perft(pos, depth - 1);
            pos.unmakeMove();
            total += n;
            std::cout << rs::moveToString(m) << ": " << n << "\n";
        }
        std::cout << "moves " << moves.size() << "  total " << total << "\n";
    }

    if (stats) {
        LeafStats s;
        perftStats(pos, depth, s);
        std::cout << "\nstats at depth " << depth << ": nodes " << s.nodes << "  captures " << s.captures
                  << "  promotions " << s.promotions << "  checks " << s.checks << "  checkmates "
                  << s.checkmates << "\n";
    }
    return 0;
}
