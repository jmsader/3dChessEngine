# Raumschach engine

A C++17 engine for **Raumschach**, the 5×5×5 three-dimensional chess variant, built as an
exploratory project on move search in 3D chess. It contains:

* a complete, validated **rules core**: board, legal move generation, make/unmake, hashing and
  game-end detection;
* a **ladder of search engines**, from random play up to alpha-beta search with move ordering,
  quiescence search and a transposition table, each one step stronger than the last;
* **learned evaluations** (a linear model and a small neural network) trained in PyTorch on
  self-play data and loaded back into the C++ engine;
* **tools** to generate training data, train models, and play engines against each other with
  Elo ratings.

The exact ruleset is in [RULES.md](RULES.md).

## Quick start

```sh
make                                  # build everything into build-make/
make test                             # unit tests + a self-play smoke test

tools/pipeline.sh --games 2000        # self-play data at depth 2, then train both models
tools/compare.sh --all --time 10      # every engine against every other, 10 ms per move
```

Training needs Python with PyTorch (see [Python setup](#python-setup)).

## Building

**Make** (GCC or Clang). This is the build the scripts use.

```sh
make            # binaries in build-make/
make test       # unit tests + self-play smoke test
make clean
```

**CMake** (Linux, macOS, Windows/Visual Studio) builds the same programs:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release     # unit tests + self-play smoke test
```

For a debug build with AddressSanitizer and UBSan (GCC/Clang), configure CMake with
`-DCMAKE_BUILD_TYPE=Debug -DRAUMSCHACH_SANITIZE=ON`. Debug builds also enable the internal
`assert`s.

### Python setup

Training uses PyTorch, NumPy and pandas, pinned in `requirements.txt`. The scripts use a
virtual environment in `venv/` if there is one:

```sh
python3 -m venv venv
venv/bin/pip install -r requirements.txt
```

A virtual environment records its own location, so if the project folder moves, recreate it.

## The engines

Every engine has a name, and the tools take engines by name. The names describe exactly which
techniques an engine uses, so each step of the ladder adds one thing:

| Engine | What it adds |
|---|---|
| `Random` | Plays a uniformly random legal move. The Elo anchor (rating 0). |
| `Minimax` | Full-width minimax search with a material evaluation. |
| `Alpha-Beta` | Alpha-beta pruning: the same result as minimax, with far fewer positions searched. |
| `Alpha-Beta+Ord` | MVV-LVA move ordering: captures of valuable pieces by cheap pieces are tried first, which makes pruning much more effective. |
| `Alpha-Beta+Ord+Q` | Quiescence search: at the end of the search, captures are played out until the position is quiet, so the evaluation is never taken in the middle of an exchange. |
| `Alpha-Beta+Ord+Q+TT` | A transposition table: positions already searched are remembered, their best move is tried first, and exact scores are reused. |

Any search engine can also use a **learned evaluation** instead of material by adding
`+LIN_d<depth>` (linear model) or `+NN_d<depth>` (neural network) to its name. The depth says
which training data the model came from. For example, `Alpha-Beta+Ord+Q+NN_d2` is the
quiescence engine with the neural network trained on depth-2 data, read from
`weights/Alpha-Beta+Ord+Q_d2_mlp.txt`.

Names are case-insensitive. The full list is printed by `match` when it's run with no engines.

### How a search works

All search engines use **iterative deepening under a time limit per move**:

1. The engine searches to depth 1, then depth 2, then depth 3, and so on.
2. While searching, it checks the clock every few nodes. The moment the time is up, it abandons
   the depth it was working on.
3. It plays the best move of the **last depth that finished** in time. If several moves are
   equally good, it picks one at random, using its own seeded random generator, so games vary
   but stay reproducible.
4. Each new depth searches the previous depth's best move first, which makes it faster.
5. If not even depth 1 finishes, which happens at very short time limits, the engine plays a
   random legal move.
6. Searching stops early once a forced mate is found.

The time limit is CPU time, so results don't depend on what else the machine is doing.
Engines always think for their whole budget.

**Quiescence search** plays out captures and promotions until no capture improves the
position. If the side to move is in check, the position isn't quiet either: it then searches
every legal escape, not just captures, and recognises checkmate. This is the standard
treatment; it makes each search depth noticeably more expensive, which costs strength at very
short time limits (about 10 ms per move or less) and pays off at longer ones.

**Material values** (in centipawns): pawn 100, unicorn 140, rook 245, bishop 295, knight 320,
queen 980.

## Learned evaluations

The learned evaluations replace material counting with a model trained to predict how good a
position is for the side to move.

**Input features.** A position is described by 1,750 yes/no features: for each square (125),
each piece type (7) and each owner (the side to move, or the opponent), is that piece on that
square? Black's positions are mirrored so that every position is seen from the side to move.

**Models.**

* **Linear** (`+LIN`): one weight per feature, so in effect a learned value for every piece on
  every square (a "piece-square table").
* **Neural network** (`+NN`): a small multilayer perceptron, 1,750 → 32 → 16 → 1, with ReLU
  activations; about 57,000 weights. It can learn combinations of features that a linear model
  can't.

**Training labels.** Each position is labelled with a mix of the game's final result and the
search score:

```
label = res_w × result + (1 − res_w) × sigmoid(score / K)      (defaults: res_w = 0.5, K = 400)
```

The result says who actually won; the score says what the search thought of the position.
Both are noisy in different ways, and mixing them averages some of that out.

**Training** (`tools/train.py`) uses PyTorch with a binary cross-entropy loss and the AdamW
optimiser. Positions are split into training and validation sets **by game**, not by position,
since positions from the same game are too similar to give an honest validation score. The
epoch with the lowest validation loss is the one kept.

**Export and inference.** The trained weights are written to a plain text file, scaled so the
output is in centipawns like the material evaluation. The C++ `Model` class loads the file once
and evaluates positions directly; it matches PyTorch's output to within rounding.

## Generating data and training

### Self-play data: `gendata`

```sh
build-make/gendata [--engine NAME] [--depth N] [--games N] [--seed S] [--random-plies K] [--out FILE]
```

The data engine (by default `Alpha-Beta+Ord+Q`) plays games against itself. Each move is
searched to a **fixed depth** (default 2) with no time limit, so every search finishes and
every position gets the same amount of search, whatever the machine.

* **Openings:** the first 4 plies are random, for variety, but chosen only among moves that
  don't lose material (judged by a quiescence search with material values). Games therefore
  start differently without being decided by an early blunder.
* **Recorded positions:** every position from the end of the random opening on.
* **Output:** one CSV row per position: `board,stm,score,result,ply,game`, written to
  `data/<engine>_d<depth>_seed<S>.csv` by default. The file's header comment documents each
  column.

### The whole pipeline: `tools/pipeline.sh`

```sh
tools/pipeline.sh [--engine NAME] [--depth N] [--games N] [--model linear|mlp|both] [--train]
```

Builds the project, runs one `gendata` process per CPU core (each `--games` games, each with
its own seed and file), then trains the linear model and the neural network on all the data for
that engine and depth.

* New runs **add** to the existing data: seeds continue after the highest one already there,
  so nothing is overwritten.
* Training always uses every file for that engine and depth, and nothing else.
* `--train` skips data generation and retrains on what's there.
* Weights go to `weights/<engine>_d<depth>_linear.txt` and `..._mlp.txt`.

Older data from the earlier, time-based data generation is kept in `data/archive/` and
`weights/archive/`; nothing reads it automatically.

## Playing engines against each other

### `match`

```sh
build-make/match ENGINE ENGINE [ENGINE ...] [--games N] [--time MS] [--seed S] [--csv FILE] [--anchor NAME] [--verbose]
build-make/match --all [options]
```

A round robin: every pair of engines plays `--games` games, alternating colours, at `--time`
milliseconds per move. `--all` ignores any engines named and plays all 16: every engine in the
ladder, and every search engine also with the depth-2 linear and neural-network models.

At the end it prints each pair's result and a table of Elo ratings, fitted to all games at once
(Bradley-Terry model; draws count half). Random, if it plays, is fixed at 0. The table also
shows each engine's average nodes searched and average depth reached per move. With `--csv
FILE`, the table is saved to `FILE` and each pair's wins, draws and losses to a file of the same
name in a `pairs/` folder next to it.

Every move an engine returns is checked against the legal moves, and each pair starts with
fresh engines, so nothing carries over between pairs.

### `tools/compare.sh`

```sh
tools/compare.sh [--time MS] [--games N] ENGINE ENGINE ...
tools/compare.sh [--time MS] [--games N] --all
```

A convenience wrapper around `match`: builds first, keeps the computer awake, and saves the
results to `results/<ms>ms.csv` (rank, engine, Elo, average depth) and
`results/pairs/<ms>ms.csv`. Defaults: 10 ms per move, 4 games per pair. Each run replaces the
files for its own time control only, so runs at different time controls can go side by side,
for example in separate terminal tabs.

## Other tools

| Program | Purpose |
|---|---|
| `perft <depth> [--text "<pos>"] [--divide] [--stats]`<br>`perft <depth> --batch < file` | Counts the leaf nodes of the legal move tree, to check move generation. `--divide` splits the count by root move; `--stats` counts captures, promotions, checks and mates at the last depth; `--batch` prints one count per input line, for comparing against `validation/ref_perft.py`. |
| `selfplay [--games N] [--seed S] [--check-all] [--dump-positions FILE]` | Random-vs-random games as a stress test of the rules core. Every ply it checks the incrementally updated state against a from-scratch recomputation, the text round trip and repetition counting; `--check-all` also makes and unmakes every legal move. Exits non-zero on any failure. |
| `unit_tests` | Tests of the rules (piece movement, pawns, promotion, pins, mate, stalemate, repetition, the 50-move rule, bare kings, start-position symmetry, text format, hashing, notation) and of the engines (legal moves, finding mate in one, deterministic play). |

## Position text format

Like FEN, adapted to three dimensions:

```
RNKNR/PPPPP/5/5/5|BUQBU/PPPPP/5/5/5|5/5/5/5/5|5/5/5/ppppp/ubqub|5/5/5/ppppp/rnknr w 0 1
```

* Levels A → E are separated by `|`; within a level, ranks 1 → 5 by `/`; within a rank, files
  a → e. (Lowest first on every axis, unlike FEN.)
* Pieces: `K Q R B N U P` for White, lowercase for Black. Digits 1–5 are empty cells.
* Then: side to move (`w`/`b`), halfmove clock and fullmove number (the last two optional).

Positions are validated on load: one king per side, no pawn on its own promotion square, and
the side not to move must not be in check.

**Moves** are written as from-square + to-square + an optional promotion letter (`q r b n u`),
e.g. `Ac2Ac3`, `Dc5Ec5q`.

## Code layout

```
include/raumschach/
  types.h      Color, Piece, Square, Move (32-bit packed), MoveList (fixed capacity)
  config.h     rule options (promotion choices, 50-move limit, repetition count)
  geometry.h   direction vectors, precomputed rays and knight/king/pawn tables, square names
  rng.h        deterministic xoshiro256** random generator (same sequence on every platform)
  zobrist.h    hashing keys (fixed seed)
  position.h   Position: board state, text I/O, attacks, make/unmake, repetition
  movegen.h    pseudo-legal / legal / noisy move generation, perft
  rules.h      game status: checkmate, stalemate, bare kings, repetition, 50-move rule
  notation.h   piece letters, move strings
  engine.h     Engine base class, Random, Minimax and its options (MinimaxOptions)
  model.h      Model: loads a trained evaluation and evaluates positions
src/           implementations of the above
tools/         perft, selfplay, match, gendata (C++); engine_table.h (engine names, shared by
               match and gendata); compare.sh, pipeline.sh, train.py
tests/         unit_tests.cpp
validation/    independent reference move generator and position sets (see validation/README.md)
data/          self-play data (not in git)
weights/       trained models (not in git)
results/       match results written by compare.sh
```

### Design notes

* **Squares are numbered 0..124** (`level*25 + rank*5 + file`). All movement geometry is
  precomputed once into read-only tables, so move generation never bounds-checks. The same
  numbering is used for the learned evaluations' input features.
* **Make/unmake with an undo stack.** `Position` keeps the hash key, king squares and
  halfmove clock in sync incrementally; `checkConsistency()` recomputes them from scratch to
  check that. The undo stack doubles as the repetition history.
* **Legal moves are pseudo-legal moves filtered by make → "is my king attacked?" → unmake.**
  Simple and hard to get wrong; a faster pin-aware generator could replace it and be checked
  against this one with perft.
* **One search class, switched by options.** Every rung of the ladder is the `Minimax` class
  with different `MinimaxOptions`, so the engines differ only in the techniques being compared.
* **Own random generator** instead of `<random>` distributions, which differ between standard
  libraries, so seeded experiments reproduce on any machine.

## Validation of the rules core

Reference perft values from the starting position:

| Depth | Nodes |
|---|---|
| 1 | 61 |
| 2 | 3,608 |
| 3 | 236,511 |
| 4 | 15,491,601 |

What has been checked:

* **Depth 1 by hand:** 61 = pawns 15 + knights 12 + queen 14 + bishops 13 + unicorns 7 (rooks
  and king are blocked). Black also has 61, since the setup is point-symmetric.
* **Against an independently written reference generator** (`validation/`): identical counts on
  the starting position to depth 3, and on 595 positions from random games (including 150 chosen
  for checks and promotions) to depths 2 and 3. About 17 million nodes, no mismatches. See
  `validation/README.md` for how to re-run it.
* **Unit tests** and **self-play with full checks** pass.
* The project builds without warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

**Still to do:** hand-check 5–10 positions (pawn captures for both colours, promotion, moves
near check and mate) and add them to `tests/unit_tests.cpp`. That is the only check that
doesn't rely on code written with AI assistance; the reference generator was also written that
way, which the write-up should say.

## Notes for interpreting results

* **Very short time limits.** At 1 ms per move, engines with quiescence often can't finish even
  depth 1 and fall back to a random move; that's a real effect of the time limit, not a bug.
* **Time limits depend on the machine.** The same number of milliseconds searches deeper on a
  faster computer, so compare results only within one machine and say which one.
* **Statistical noise.** With a few dozen games per pair, Elo differences under about 50–100 are
  within the noise; check the ±95% column before drawing conclusions.
