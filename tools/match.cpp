// match: round-robin between two or more engines; every pair plays the same number of games.
//
// Usage:
//   match <engine> <engine> [<engine> ...] [--games N] [--seed S] [--csv FILE] [--verbose]
//
//   engines   any of: random greedy   (the same name may appear twice, e.g. "greedy greedy")
//   --games   games per pair (default 10); colours alternate, so use an even number
//   --seed    seed for std::rand (default 1); the same command gives the same results
//   --csv     also write one row per game to FILE (for tables and plots in the write-up)
//   --verbose print every move and the final board of each game
//
// Each pair gets fresh engine objects, so no state (e.g. a future hash table) carries over
// between pairs. Every move an engine returns is checked against the legal moves; an engine
// that returns an illegal or null move in a live position loses that game.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "raumschach/engine.h"
#include "raumschach/movegen.h"
#include "raumschach/notation.h"
#include "raumschach/position.h"
#include "raumschach/rules.h"

namespace {

// Add new engines here (one line each).
const std::vector<std::string> kEngineNames = {"random", "greedy"};

std::unique_ptr<rs::Engine> makeEngine(const std::string& name) {
    if (name == "random") return std::make_unique<rs::Random>();
    if (name == "greedy") return std::make_unique<rs::Greedy>();
    return nullptr;
}

enum class Outcome { WhiteWins, BlackWins, Draw };

struct GameRecord {
    Outcome outcome;
    std::string reason;
    int plies;
};

GameRecord playGame(rs::Engine& white, rs::Engine& black, bool verbose) {
    rs::Position pos;  // one Position for the whole game, so repetition history works
    int plies = 0;
    for (;;) {
        const rs::GameStatus status = rs::gameStatus(pos);
        if (status.isOver()) {
            if (verbose) std::cout << "\n" << pos.diagram();
            const Outcome o = status.result == rs::GameResult::WhiteWins   ? Outcome::WhiteWins
                              : status.result == rs::GameResult::BlackWins ? Outcome::BlackWins
                                                                           : Outcome::Draw;
            return {o, rs::toString(status.reason), plies};
        }

        rs::Engine& toMove = pos.sideToMove() == rs::White ? white : black;
        const auto [move, score] = toMove.search(pos);

        rs::MoveList legal;
        rs::generateLegalMoves(pos, legal);
        if (move.isNull() || !legal.contains(move)) {
            std::cerr << toMove.name << " returned illegal move " << rs::moveToString(move) << " in:\n"
                      << pos.diagram() << pos.toText() << "\n";
            const Outcome o = pos.sideToMove() == rs::White ? Outcome::BlackWins : Outcome::WhiteWins;
            return {o, "illegal move", plies};
        }

        if (verbose) {
            if (pos.sideToMove() == rs::White) std::cout << pos.fullmoveNumber() << ". ";
            std::cout << rs::moveToString(move) << " (" << score << ") ";
        }
        pos.makeMove(move);
        ++plies;
    }
}

std::string resultText(Outcome o) {
    return o == Outcome::Draw ? "1/2-1/2" : o == Outcome::WhiteWins ? "1-0" : "0-1";
}

// Elo difference implied by a score fraction (only defined strictly between 0 and 1).
std::string eloText(double score) {
    if (score <= 0.0 || score >= 1.0) return "n/a";
    const long elo = std::lround(-400.0 * std::log10(1.0 / score - 1.0));
    return (elo > 0 ? "+" : "") + std::to_string(elo);
}

struct Standing {
    double points = 0;
    int games = 0, wins = 0, draws = 0, losses = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> names;
    int games = 10;
    unsigned seed = 1;
    std::string csvFile;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--games" && i + 1 < argc) games = std::atoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (arg == "--csv" && i + 1 < argc) csvFile = argv[++i];
        else if (arg == "--verbose") verbose = true;
        else if (!arg.empty() && arg[0] == '-') { std::cerr << "unknown option: " << arg << "\n"; return 2; }
        else names.push_back(arg);
    }

    std::string available;
    for (const auto& n : kEngineNames) available += " " + n;
    if (names.size() < 2 || games < 1) {
        std::cerr << "usage: match <engine> <engine> [<engine> ...] [--games N] [--seed S] [--csv FILE] [--verbose]\n"
                  << "engines:" << available << "\n";
        return 2;
    }
    for (const auto& n : names)
        if (!makeEngine(n)) { std::cerr << "unknown engine: " << n << " (choose from:" << available << ")\n"; return 2; }

    // Labels for the report: a name given more than once becomes "greedy#1", "greedy#2", ...
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto total = std::count(names.begin(), names.end(), names[i]);
        const auto nth = std::count(names.begin(), names.begin() + static_cast<long>(i) + 1, names[i]);
        labels.push_back(total > 1 ? names[i] + "#" + std::to_string(nth) : names[i]);
    }

    std::ofstream csv;
    if (!csvFile.empty()) {
        csv.open(csvFile);
        if (!csv) { std::cerr << "cannot write " << csvFile << "\n"; return 2; }
        csv << "pair,game,white,black,result,reason,plies,seed\n";
    }

    std::srand(seed);
    std::map<std::string, Standing> table;
    int pairIndex = 0;

    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j, ++pairIndex) {
            auto a = makeEngine(names[i]), b = makeEngine(names[j]);  // fresh objects per pair
            const std::string& la = labels[i];
            const std::string& lb = labels[j];
            int winsA = 0, winsB = 0, draws = 0;
            std::map<std::string, int> reasons;

            for (int g = 0; g < games; ++g) {
                const bool aIsWhite = (g % 2 == 0);  // swap colours every game
                rs::Engine& white = aIsWhite ? *a : *b;
                rs::Engine& black = aIsWhite ? *b : *a;
                if (verbose)
                    std::cout << "\n" << la << " vs " << lb << ", game " << g + 1 << ": "
                              << (aIsWhite ? la : lb) << " (White) vs " << (aIsWhite ? lb : la) << " (Black)\n";

                const GameRecord r = playGame(white, black, verbose);
                ++reasons[r.reason];
                if (r.outcome == Outcome::Draw) ++draws;
                else if ((r.outcome == Outcome::WhiteWins) == aIsWhite) ++winsA;
                else ++winsB;

                if (verbose) std::cout << "\n=> " << resultText(r.outcome) << " by " << r.reason << " after " << r.plies << " plies\n";
                if (csv.is_open())
                    csv << pairIndex << "," << g + 1 << "," << (aIsWhite ? la : lb) << "," << (aIsWhite ? lb : la) << ","
                        << resultText(r.outcome) << "," << r.reason << "," << r.plies << "," << seed << "\n";
            }

            const double score = (winsA + 0.5 * draws) / games;
            std::ostringstream why;
            for (const auto& [reason, count] : reasons) why << (why.tellp() > 0 ? ", " : "") << reason << " " << count;
            std::cout << std::left << std::setw(12) << la << " vs " << std::setw(12) << lb << "  +" << winsA << " =" << draws
                      << " -" << winsB << "  score " << std::fixed << std::setprecision(1) << 100.0 * score << "%  Elo "
                      << eloText(score) << "   (" << why.str() << ")\n";

            Standing& sa = table[la];
            Standing& sb = table[lb];
            sa.games += games; sb.games += games;
            sa.wins += winsA; sa.draws += draws; sa.losses += winsB;
            sb.wins += winsB; sb.draws += draws; sb.losses += winsA;
            sa.points += winsA + 0.5 * draws;
            sb.points += winsB + 0.5 * draws;
        }
    }

    if (names.size() > 2) {
        std::vector<std::pair<std::string, Standing>> rows(table.begin(), table.end());
        std::sort(rows.begin(), rows.end(), [](const auto& x, const auto& y) { return x.second.points > y.second.points; });
        std::cout << "\nStandings (" << games << " games per pair)\n";
        for (const auto& [label, s] : rows)
            std::cout << "  " << std::left << std::setw(12) << label << std::right << std::setw(6) << std::setprecision(1)
                      << s.points << " / " << s.games << "   +" << s.wins << " =" << s.draws << " -" << s.losses << "   "
                      << std::setprecision(1) << 100.0 * s.points / s.games << "%\n";
    }
    if (csv.is_open()) std::cout << "\nwrote " << csvFile << "\n";
    return 0;
}
