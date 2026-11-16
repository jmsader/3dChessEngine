// match: round-robin between two or more engines; every pair plays the same number of games.
//
// Usage:
//   match <engine> <engine> [<engine> ...] [--games N] [--seed S] [--time S] [--csv FILE] [--anchor NAME] [--verbose]
//
//   engines   Random, Minimax, Alpha-Beta, Alpha-Beta+Ord: the engines' own names, in any
//             capitalization. All output uses the engine's name; the same engine may appear
//             twice ("Minimax Minimax" -> Minimax#1, Minimax#2).
//   --games   games per pair (default 10); colours alternate, so use an even number
//   --seed    master seed (default 1). Every engine object gets its own seed derived from it,
//             so the same command gives the same results on any machine, and engines never
//             share a random sequence (e.g. both sides of "Minimax Minimax" choose independently).
//             Searches are timed, so results also depend on machine speed and load: the same
//             command can give different games on another run.
//   --time    global time control: whole seconds per move for every Minimax engine (sets
//             MinimaxOptions::secs_per_move; without it, the engines' default is used)
//   --csv     write one row per game to FILE, including both engines' final Elo, and the
//             final ratings to FILE with "_elo" before the extension (e.g. results_elo.csv)
//   --anchor  engine whose Elo is fixed at 0 (default: random if it plays, else the first engine listed)
//   --verbose print every move and the final board of each game
//
// Output: each pair's result with its Elo difference and 95% range, then the final Elo table,
// fitted to all games at once (Elo/Bradley-Terry model, draws count 1/2). One virtual draw per
// pair is added so that a perfect score still gives a finite rating; its effect fades as the
// number of games grows. The +-95% ranges are approximate and relative to the anchor engine.
//
// Each pair gets fresh engine objects, so no state (e.g. a future hash table) carries over
// between pairs. Every move an engine returns is checked against the legal moves; an engine
// that returns an illegal or null move in a live position loses that game.
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
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

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// How to create each engine. Add new engines here (one line each). The name you type, and the
// name shown in all output, is the engine's own `name` member, so renaming an engine in its
// constructor updates the match tool automatically.
using EngineFactory = std::function<std::unique_ptr<rs::Engine>()>;

int g_secsPerMove = 0;  // from --time; 0 = keep MinimaxOptions' default

// Options with the given switches and the match's time control.
rs::MinimaxOptions minimaxOptions(bool pruning, bool ordering) {
    rs::MinimaxOptions o;
    o.pruning = pruning;
    o.ordering = ordering;
    if (g_secsPerMove > 0) o.secs_per_move = g_secsPerMove;
    return o;
}

const std::vector<EngineFactory>& engineTable() {
    static const std::vector<EngineFactory> table = {
        [] { return std::make_unique<rs::Random>(); },
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(false, false)); },  // Minimax
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, false)); },   // Alpha-Beta
        [] { return std::make_unique<rs::Minimax>(minimaxOptions(true, true)); },    // Alpha-Beta+Ord
    };
    return table;
}

// Creates an engine from its name (case-insensitive), e.g. "Random", "alpha-beta+ord".
std::unique_ptr<rs::Engine> makeEngine(const std::string& name) {
    for (const EngineFactory& make : engineTable()) {
        auto engine = make();
        if (lower(engine->name) == lower(name)) return engine;
    }
    return nullptr;
}

// "Random Minimax Alpha-Beta Alpha-Beta+Ord", for usage messages.
std::string availableEngines() {
    std::string out;
    for (const EngineFactory& make : engineTable()) out += " " + make()->name;
    return out;
}

enum class Outcome { WhiteWins, BlackWins, Draw };

struct GameRecord {
    Outcome outcome;
    std::string reason;
    int plies;
};

// Search effort of one engine over all its moves.
struct Usage {
    std::uint64_t nodes = 0;
    long moves = 0;
    double seconds = 0.0;
    long depthSum = 0;  // sum of depth_reached over all moves
};

GameRecord playGame(rs::Engine& white, rs::Engine& black, Usage& whiteUse, Usage& blackUse, bool verbose) {
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
        const auto start = std::chrono::steady_clock::now();
        const auto [move, score] = toMove.search(pos);
        Usage& use = pos.sideToMove() == rs::White ? whiteUse : blackUse;
        use.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        use.nodes += toMove.nodes;
        use.depthSum += toMove.depth_reached;
        ++use.moves;

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

constexpr double kEloPerNat = 400.0 / 2.302585092994046;  // 400 / ln(10)
constexpr double kPriorDraws = 1.0;                       // virtual draws added per pair

std::string signedInt(double x) {
    const long v = std::lround(x);
    return (v > 0 ? "+" : "") + std::to_string(v);
}

// Elo difference implied by a match score, with a 95% range from the W/D/L variance.
std::string pairEloText(int w, int d, int l) {
    const int n = w + d + l;
    const double s = (w + 0.5 * d) / n;
    if (s <= 0.0) return "n/a (no points)";
    if (s >= 1.0) return "n/a (perfect score)";
    const double var = (w + 0.25 * d) / n - s * s;
    if (var <= 0.0) return signedInt(-400.0 * std::log10(1.0 / s - 1.0)) + " [no range: every game drawn]";
    const double sigma = std::sqrt(var / n);
    auto toElo = [](double x) { return -400.0 * std::log10(1.0 / x - 1.0); };
    const double lo = s - 1.96 * sigma, hi = s + 1.96 * sigma;
    return signedInt(toElo(s)) + " [" + (lo <= 0.0 ? std::string("-inf") : signedInt(toElo(lo))) + ", " +
           (hi >= 1.0 ? std::string("+inf") : signedInt(toElo(hi))) + "]";
}

struct PairResult {
    std::size_t a, b;
    int winsA = 0, draws = 0, winsB = 0;
};

struct GameRow {
    int pair, game;
    std::size_t white, black;
    Outcome outcome;
    std::string reason;
    int plies;
};

// Gauss-Jordan inverse of a small matrix; empty result if it is singular.
std::vector<std::vector<double>> invert(std::vector<std::vector<double>> a) {
    const std::size_t m = a.size();
    std::vector<std::vector<double>> inv(m, std::vector<double>(m, 0.0));
    for (std::size_t i = 0; i < m; ++i) inv[i][i] = 1.0;
    for (std::size_t c = 0; c < m; ++c) {
        std::size_t piv = c;
        for (std::size_t r = c + 1; r < m; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return {};
        std::swap(a[c], a[piv]);
        std::swap(inv[c], inv[piv]);
        const double d = a[c][c];
        for (std::size_t k = 0; k < m; ++k) { a[c][k] /= d; inv[c][k] /= d; }
        for (std::size_t r = 0; r < m; ++r) {
            const double f = a[r][c];
            if (r == c || f == 0.0) continue;
            for (std::size_t k = 0; k < m; ++k) { a[r][k] -= f * a[c][k]; inv[r][k] -= f * inv[c][k]; }
        }
    }
    return inv;
}

struct Ratings {
    std::vector<double> elo;   // relative to the anchor (anchor = 0)
    std::vector<double> ci95;  // half-width of the ~95% range; 0 for the anchor, NaN if unknown
};

// Fits one Elo rating per engine to all pair results at once (maximum likelihood under the
// Elo / Bradley-Terry model, draws as half points), using the standard MM iteration.
Ratings fitElo(std::size_t n, const std::vector<PairResult>& pairs, std::size_t anchor) {
    std::vector<std::vector<double>> games(n, std::vector<double>(n, 0.0));
    std::vector<double> points(n, 0.0);
    for (const PairResult& p : pairs) {
        const double g = p.winsA + p.draws + p.winsB + kPriorDraws;
        games[p.a][p.b] += g;
        games[p.b][p.a] += g;
        points[p.a] += p.winsA + 0.5 * (p.draws + kPriorDraws);
        points[p.b] += p.winsB + 0.5 * (p.draws + kPriorDraws);
    }

    std::vector<double> strength(n, 1.0);  // 10^(elo/400)
    for (int iter = 0; iter < 100000; ++iter) {
        double change = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double denom = 0.0;
            for (std::size_t j = 0; j < n; ++j)
                if (j != i && games[i][j] > 0.0) denom += games[i][j] / (strength[i] + strength[j]);
            if (denom <= 0.0) continue;
            const double updated = points[i] / denom;
            change = std::max(change, std::fabs(std::log(updated / strength[i])));
            strength[i] = updated;
        }
        const double s0 = strength[anchor];
        for (double& x : strength) x /= s0;
        if (change < 1e-12) break;
    }

    Ratings r;
    for (std::size_t i = 0; i < n; ++i) r.elo.push_back(kEloPerNat * std::log(strength[i]));

    // Uncertainty: inverse Fisher information of the log-strengths, anchor held fixed.
    // Draws are treated like half wins here, so the ranges are, if anything, slightly too wide.
    std::vector<std::size_t> others;
    for (std::size_t i = 0; i < n; ++i)
        if (i != anchor) others.push_back(i);
    const std::size_t m = others.size();
    std::vector<std::vector<double>> info(m, std::vector<double>(m, 0.0));
    for (std::size_t x = 0; x < m; ++x) {
        const std::size_t i = others[x];
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || games[i][j] <= 0.0) continue;
            const double p = strength[i] / (strength[i] + strength[j]);
            const double w = games[i][j] * p * (1.0 - p);
            info[x][x] += w;
            for (std::size_t y = 0; y < m; ++y)
                if (others[y] == j) info[x][y] -= w;
        }
    }
    const auto cov = invert(info);
    r.ci95.assign(n, 0.0);
    for (std::size_t x = 0; x < m; ++x)
        r.ci95[others[x]] = cov.empty() ? std::nan("") : 1.96 * kEloPerNat * std::sqrt(cov[x][x]);
    return r;
}

// Distinct, well-mixed seed for one engine object: (master seed, pair, side) -> 64 bits.
std::uint64_t engineSeed(unsigned master, int pair, int side) {
    std::uint64_t z = (static_cast<std::uint64_t>(master) << 32) ^ (static_cast<std::uint64_t>(pair) << 1) ^
                      static_cast<std::uint64_t>(side);
    z += 0x9E3779B97F4A7C15ull;  // splitmix64 finaliser
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// "results.csv" -> "results_elo.csv"; "out" -> "out_elo.csv"
std::string ratingsFileName(const std::string& csvFile) {
    const auto dot = csvFile.find_last_of('.');
    const auto slash = csvFile.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return csvFile + "_elo.csv";
    return csvFile.substr(0, dot) + "_elo" + csvFile.substr(dot);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> names;
    int games = 10;
    unsigned seed = 1;
    std::string csvFile, anchorName;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--games" && i + 1 < argc) games = std::atoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (arg == "--time" && i + 1 < argc) {
            g_secsPerMove = std::atoi(argv[++i]);
            if (g_secsPerMove < 1) { std::cerr << "--time needs a whole number of seconds >= 1\n"; return 2; }
        }
        else if (arg == "--csv" && i + 1 < argc) csvFile = argv[++i];
        else if (arg == "--anchor" && i + 1 < argc) anchorName = argv[++i];
        else if (arg == "--verbose") verbose = true;
        else if (!arg.empty() && arg[0] == '-') { std::cerr << "unknown option: " << arg << "\n"; return 2; }
        else names.push_back(arg);
    }

    const std::string available = availableEngines();
    if (names.size() < 2 || games < 1) {
        std::cerr << "usage: match <engine> <engine> [<engine> ...] [--games N] [--seed S] [--time S] [--csv FILE] [--anchor NAME] [--verbose]\n"
                  << "engines:" << available << "\n";
        return 2;
    }
    for (const auto& n : names)
        if (!makeEngine(n)) { std::cerr << "unknown engine: " << n << " (choose from:" << available << ")\n"; return 2; }

    // Labels for the report: each engine's own name (e.g. "Alpha-Beta"); a name that appears
    // more than once becomes "Minimax#1", "Minimax#2", ...
    std::vector<std::string> baseNames;
    for (const auto& n : names) baseNames.push_back(makeEngine(n)->name);
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < baseNames.size(); ++i) {
        const auto total = std::count(baseNames.begin(), baseNames.end(), baseNames[i]);
        const auto nth = std::count(baseNames.begin(), baseNames.begin() + static_cast<long>(i) + 1, baseNames[i]);
        labels.push_back(total > 1 ? baseNames[i] + "#" + std::to_string(nth) : baseNames[i]);
    }
    int nameWidth = 12;  // engine-name column: at least 12, wider for longer names
    for (const auto& l : labels) nameWidth = std::max(nameWidth, static_cast<int>(l.size()) + 1);

    // Default anchor: the (first) random engine if it plays, otherwise the first engine listed.
    std::size_t anchor = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (lower(baseNames[i]) == "random") { anchor = i; break; }
    if (!anchorName.empty()) {
        bool found = false;
        for (std::size_t i = 0; i < labels.size() && !found; ++i)
            if (lower(labels[i]) == lower(anchorName) || lower(baseNames[i]) == lower(anchorName)) { anchor = i; found = true; }
        if (!found) { std::cerr << "--anchor " << anchorName << " is not one of the engines in this match\n"; return 2; }
    }

    std::ofstream csv, ratingsCsv;
    const std::string ratingsFile = csvFile.empty() ? "" : ratingsFileName(csvFile);
    if (!csvFile.empty()) {
        csv.open(csvFile);
        ratingsCsv.open(ratingsFile);
        if (!csv || !ratingsCsv) { std::cerr << "cannot write " << csvFile << " / " << ratingsFile << "\n"; return 2; }
    }

    std::vector<PairResult> pairs;
    std::vector<GameRow> rows;
    std::vector<Usage> usage(names.size());  // per engine, summed over all its games
    int pairIndex = 0;

    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j, ++pairIndex) {
            auto a = makeEngine(names[i]), b = makeEngine(names[j]);  // fresh objects per pair
            a->seed(engineSeed(seed, pairIndex, 0));
            b->seed(engineSeed(seed, pairIndex, 1));
            const std::string& la = labels[i];
            const std::string& lb = labels[j];
            PairResult pr{i, j};
            std::map<std::string, int> reasons;

            for (int g = 0; g < games; ++g) {
                const bool aIsWhite = (g % 2 == 0);  // swap colours every game
                rs::Engine& white = aIsWhite ? *a : *b;
                rs::Engine& black = aIsWhite ? *b : *a;
                if (verbose)
                    std::cout << "\n" << la << " vs " << lb << ", game " << g + 1 << ": "
                              << (aIsWhite ? la : lb) << " (White) vs " << (aIsWhite ? lb : la) << " (Black)\n";

                const GameRecord r = playGame(white, black, aIsWhite ? usage[i] : usage[j],
                                              aIsWhite ? usage[j] : usage[i], verbose);
                ++reasons[r.reason];
                if (r.outcome == Outcome::Draw) ++pr.draws;
                else if ((r.outcome == Outcome::WhiteWins) == aIsWhite) ++pr.winsA;
                else ++pr.winsB;

                if (verbose) std::cout << "\n=> " << resultText(r.outcome) << " by " << r.reason << " after " << r.plies << " plies\n";
                rows.push_back({pairIndex, g + 1, aIsWhite ? i : j, aIsWhite ? j : i, r.outcome, r.reason, r.plies});
            }

            const double score = (pr.winsA + 0.5 * pr.draws) / games;
            std::ostringstream why;
            for (const auto& [reason, count] : reasons) why << (why.tellp() > 0 ? ", " : "") << reason << " " << count;
            std::cout << std::left << std::setw(nameWidth) << la << " vs " << std::setw(nameWidth) << lb << "  +" << pr.winsA << " ="
                      << pr.draws << " -" << pr.winsB << "  score " << std::fixed << std::setprecision(1) << 100.0 * score
                      << "%  Elo " << pairEloText(pr.winsA, pr.draws, pr.winsB) << "   (" << why.str() << ")\n";
            pairs.push_back(pr);
        }
    }

    // ---------------------------------------------------------------- final Elo table
    const std::size_t n = names.size();
    const Ratings ratings = fitElo(n, pairs, anchor);

    std::vector<int> wins(n, 0), draws(n, 0), losses(n, 0);
    for (const PairResult& p : pairs) {
        wins[p.a] += p.winsA; draws[p.a] += p.draws; losses[p.a] += p.winsB;
        wins[p.b] += p.winsB; draws[p.b] += p.draws; losses[p.b] += p.winsA;
    }
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return ratings.elo[x] > ratings.elo[y]; });

    auto nodesPerMove = [&](std::size_t i) { return usage[i].moves ? double(usage[i].nodes) / double(usage[i].moves) : 0.0; };
    auto msPerMove = [&](std::size_t i) { return usage[i].moves ? 1000.0 * usage[i].seconds / double(usage[i].moves) : 0.0; };
    auto depthPerMove = [&](std::size_t i) { return usage[i].moves ? double(usage[i].depthSum) / double(usage[i].moves) : 0.0; };

    auto ciText = [&](std::size_t i) -> std::string {
        if (i == anchor) return "anchor";
        if (std::isnan(ratings.ci95[i])) return "?";
        return "+-" + std::to_string(std::lround(ratings.ci95[i]));
    };

    std::cout << "\nFinal Elo (" << labels[anchor] << " = 0; " << games << " games per pair; "
              << (g_secsPerMove > 0 ? std::to_string(g_secsPerMove) + " s per move; " : std::string())
              << "+-95%)\n"
              << "   #  " << std::left << std::setw(nameWidth) << "engine"
              << "    Elo     +-95%   games  points   score      W     D     L   nodes/move  ms/move  depth/move\n";
    for (std::size_t r = 0; r < n; ++r) {
        const std::size_t i = order[r];
        const int played = wins[i] + draws[i] + losses[i];
        const double pts = wins[i] + 0.5 * draws[i];
        std::cout << std::right << std::setw(4) << r + 1 << "  " << std::left << std::setw(nameWidth) << labels[i] << std::right
                  << std::setw(7) << signedInt(ratings.elo[i]) << std::setw(10) << ciText(i) << std::setw(8) << played
                  << std::setw(8) << std::setprecision(1) << pts << std::setw(7) << 100.0 * pts / played << "%"
                  << std::setw(7) << wins[i] << std::setw(6) << draws[i] << std::setw(6) << losses[i]
                  << std::setw(13) << std::setprecision(0) << nodesPerMove(i) << std::setw(9) << std::setprecision(2)
                  << msPerMove(i) << std::setw(12) << std::setprecision(1) << depthPerMove(i) << "\n";
    }
    std::cout << "(fitted to all games at once; draws count 1/2; 1 virtual draw per pair keeps perfect scores finite;\n"
              << " nodes/move, ms/move and depth/move are averages over all of an engine's moves in this match)\n";

    if (csv.is_open()) {
        csv << "pair,game,white,black,result,reason,plies,seed,white_elo,black_elo\n";
        for (const GameRow& g : rows)
            csv << g.pair << "," << g.game << "," << labels[g.white] << "," << labels[g.black] << "," << resultText(g.outcome)
                << "," << g.reason << "," << g.plies << "," << seed << "," << std::lround(ratings.elo[g.white]) << ","
                << std::lround(ratings.elo[g.black]) << "\n";

        ratingsCsv << std::fixed << "rank,engine,elo,ci95,anchor,games,points,score,wins,draws,losses,games_per_pair,seed,nodes_per_move,ms_per_move,depth_per_move\n";
        for (std::size_t r = 0; r < n; ++r) {
            const std::size_t i = order[r];
            const int played = wins[i] + draws[i] + losses[i];
            const double pts = wins[i] + 0.5 * draws[i];
            ratingsCsv << r + 1 << "," << labels[i] << "," << std::lround(ratings.elo[i]) << ","
                       << (i == anchor ? std::string("0") : std::isnan(ratings.ci95[i]) ? std::string("")
                                                                                         : std::to_string(std::lround(ratings.ci95[i])))
                       << "," << labels[anchor] << "," << played << "," << std::setprecision(1) << pts << ","
                       << std::setprecision(3) << pts / played << "," << wins[i] << "," << draws[i] << "," << losses[i] << ","
                       << games << "," << seed << "," << std::setprecision(1) << nodesPerMove(i) << ","
                       << std::setprecision(3) << msPerMove(i) << "," << std::setprecision(2) << depthPerMove(i) << "\n";
        }
        std::cout << "\nwrote " << csvFile << " (per game, with final Elo) and " << ratingsFile << " (final Elo table)\n";
    }
    return 0;
}
