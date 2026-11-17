// gendata: self-play games that save training positions for a learned evaluation.
//
// Usage:
//   gendata [--engine NAME] [--time MS] [--games N] [--seed S] [--random-plies K] [--out FILE]
//
//   --engine        engine playing both sides, by its match name (default Alpha-Beta+Ord+Q)
//   --time          milliseconds per move (default 10)
//   --games         number of games (default 100)
//   --seed          seed for the random opening moves and the engines' tie-breaking (default 1);
//                   run several processes with different seeds (and files) to use more cores
//   --random-plies  plies played at random at the start of every game, for variety (default 6)
//   --out           output file (default data/<engine>_<time>ms_seed<S>.csv, e.g.
//                   data/Alpha-Beta+Ord+Q_10ms_seed1.csv), so every engine x time control
//                   configuration gets its own file
//
// One CSV row per kept position:
//   board,stm,score,result,ply,game
//   board   125 characters, square 0..124 (index = level*25 + rank*5 + file), one piece letter
//           per square: PNBRUQK White, pnbruqk Black, '.' empty
//   stm     side to move: w or b
//   score   the engine's search score for the position, in centipawns, for the side to move
//   result  the game's result for the side to move: 1 win, 0.5 draw, 0 loss
//   ply     ply number in the game (0 = start position)
//   game    game number in this file
//
// Kept positions are those that are quiet enough to label: after the random opening plus two
// more plies, side to move not in check, the engine's chosen move not a capture or promotion,
// and the score not a mate score. Rows are written at the end of each game (the result is
// known only then), so a stopped run keeps every finished game.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "engine_table.h"
#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/notation.h"
#include "raumschach/position.h"
#include "raumschach/rng.h"
#include "raumschach/rules.h"

namespace {

// splitmix64: a different, reproducible seed for every game and side.
std::uint64_t mixSeed(std::uint64_t seed, std::uint64_t game, std::uint64_t side) {
    std::uint64_t z = seed * 0x9E3779B97F4A7C15ull + game * 0xD1B54A32D192ED03ull + side + 1;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string boardString(const rs::Position& pos) {
    std::string s(rs::kNumSquares, '.');
    for (rs::Square sq = 0; sq < rs::kNumSquares; ++sq) s[static_cast<std::size_t>(sq)] = rs::pieceToChar(pos.pieceAt(sq));
    return s;
}

struct Row {
    std::string board;
    rs::Color stm;
    int score;
    int ply;
};

}  // namespace

int main(int argc, char** argv) {
    std::string engineName = "Alpha-Beta+Ord+Q", outFile;
    int games = 100, randomPlies = 6;
    std::uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--engine" && i + 1 < argc) engineName = argv[++i];
        else if (arg == "--time" && i + 1 < argc) g_msPerMove = std::atoi(argv[++i]);
        else if (arg == "--games" && i + 1 < argc) games = std::atoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--random-plies" && i + 1 < argc) randomPlies = std::atoi(argv[++i]);
        else if (arg == "--out" && i + 1 < argc) outFile = argv[++i];
        else {
            std::cerr << "usage: gendata [--engine NAME] [--time MS] [--games N] [--seed S] [--random-plies K] [--out FILE]\n"
                      << "engines:" << availableEngines() << "\n";
            return 2;
        }
    }
    if (g_msPerMove <= 0) g_msPerMove = 10;
    if (games < 1 || randomPlies < 0) { std::cerr << "--games must be >= 1 and --random-plies >= 0\n"; return 2; }

    auto white = makeEngine(engineName), black = makeEngine(engineName);
    if (!white) { std::cerr << "unknown engine: " << engineName << " (choose from:" << availableEngines() << ")\n"; return 2; }

    if (outFile.empty()) {
        std::filesystem::create_directories("data");
        outFile = "data/" + white->name + "_" + std::to_string(g_msPerMove) + "ms_seed" + std::to_string(seed) + ".csv";
    }
    std::ofstream out(outFile);
    if (!out) { std::cerr << "cannot write " << outFile << "\n"; return 2; }
    out << "board,stm,score,result,ply,game\n";

    const auto start = std::chrono::steady_clock::now();
    long kept = 0;
    int wins[3] = {0, 0, 0};  // white wins, black wins, draws
    for (int g = 0; g < games; ++g) {
        rs::Rng openingRng(mixSeed(seed, static_cast<std::uint64_t>(g), 2));
        white->seed(mixSeed(seed, static_cast<std::uint64_t>(g), 0));
        black->seed(mixSeed(seed, static_cast<std::uint64_t>(g), 1));

        rs::Position pos;
        std::vector<Row> rows;
        rs::GameStatus status;
        int ply = 0;
        for (;; ++ply) {
            status = rs::gameStatus(pos);
            if (status.isOver()) break;
            rs::Move move;
            if (ply < randomPlies) {
                rs::MoveList moves;
                rs::generateLegalMoves(pos, moves);
                move = moves[static_cast<std::size_t>(openingRng.below(moves.size()))];
            } else {
                rs::Engine& toMove = pos.sideToMove() == rs::White ? *white : *black;
                const auto [m, score] = toMove.search(pos);
                move = m;
                const bool noisy = rs::typeOf(pos.pieceAt(move.to())) != rs::NoPieceType ||
                                   move.promotion() != rs::NoPieceType;
                if (ply >= randomPlies + 2 && !pos.inCheck() && !noisy && !rs::Engine::is_mate_cpawns(score))
                    rows.push_back({boardString(pos), pos.sideToMove(), score, ply});
            }
            pos.makeMove(move);
        }

        const bool draw = status.result == rs::GameResult::Draw;
        const rs::Color winner = status.result == rs::GameResult::WhiteWins ? rs::White : rs::Black;
        ++wins[draw ? 2 : winner == rs::White ? 0 : 1];
        for (const Row& r : rows)
            out << r.board << ',' << (r.stm == rs::White ? 'w' : 'b') << ',' << r.score << ','
                << (draw ? "0.5" : r.stm == winner ? "1" : "0") << ',' << r.ply << ',' << g << '\n';
        out.flush();
        kept += static_cast<long>(rows.size());

        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cerr << "game " << g + 1 << "/" << games << ": " << rs::toString(status.reason) << " after " << ply
                  << " plies, " << rows.size() << " positions kept (total " << kept << ", " << static_cast<int>(secs)
                  << " s)\n";
    }
    std::cerr << "done: " << games << " games (White " << wins[0] << ", Black " << wins[1] << ", draws " << wins[2]
              << "), " << kept << " positions -> " << outFile << "\n";
    return 0;
}
