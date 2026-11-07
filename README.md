# Raumschach engine: rules core

C++17 implementation of the Raumschach (5×5×5 3D chess) rules: board, legal move
generation, make/unmake, hashing and game-end detection, plus tools to validate them.
Search, evaluation and the match harness will be built on top of this.

The exact ruleset is in [RULES.md](RULES.md).

## Build

**CMake** (Linux, macOS, Windows/Visual Studio):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release        # unit tests + self-play smoke test
```

**Make** (GCC or Clang, no CMake needed):

```sh
make            # binaries in build-make/
make test
```

For a debug build with AddressSanitizer and UBSan (GCC/Clang), configure with
`-DCMAKE_BUILD_TYPE=Debug -DRAUMSCHACH_SANITIZE=ON`. Debug builds also enable the
internal `assert`s.

## Tools

| Program | Purpose |
|---------|---------|
| `perft <depth> [--text "<pos>"] [--divide] [--stats]`<br>`perft <depth> --batch < file` | Count leaf nodes of the legal move tree. `--divide` splits the count by root move; `--stats` counts captures, promotions, checks and mates at the last depth; `--batch` prints one count per input line (for diffing against `validation/ref_perft.py`). |
| `selfplay [--games N] [--seed S] [--check-all] [--dump-positions FILE]` | Random-vs-random games as a stress test. Every ply it checks incremental state against a from-scratch recomputation, the text round trip, and repetition counting; `--check-all` also makes/unmakes every legal move and checks noisy-move generation. Exits non-zero on any failure. |
| `unit_tests` | Rule-level tests (piece mobility, pawn rules, promotion, pins, mate, stalemate, repetition, 50-move rule, bare kings, start-position symmetry, text format, hashing, notation). |

## Position text format

Like FEN, adapted to three dimensions:

```
RNKNR/PPPPP/5/5/5|BUQBU/PPPPP/5/5/5|5/5/5/5/5|5/5/5/ppppp/ubqub|5/5/5/ppppp/rnknr w 0 1
```

* Levels A → E separated by `|`; within a level, ranks 1 → 5 separated by `/`;
  within a rank, files a → e. (Lowest first on every axis, unlike FEN.)
* Pieces: `K Q R B N U P` for White, lowercase for Black. Digits 1–5 = empty cells.
* Then: side to move (`w`/`b`), halfmove clock, fullmove number (the last two are optional).

Positions are validated on load: one king per side, no pawn on its own promotion
square, and the side not to move must not be in check.

**Moves** are written from-square + to-square + optional promotion letter (`q r b n u`),
e.g. `Ac2Ac3`, `Dc5Ec5q`.

## Code layout

```
include/raumschach/
  types.h      Color, Piece, Square, Move (32-bit packed), MoveList (fixed capacity)
  config.h     rule options (promotion choices, 50-move limit, repetition count)
  geometry.h   direction vectors, precomputed rays / knight / king / pawn tables, square names
  rng.h        deterministic xoshiro256** PRNG (same sequence on every platform)
  zobrist.h    hashing keys (fixed seed)
  position.h   Position: board state, text I/O, attacks, make/unmake, repetition
  movegen.h    pseudo-legal / legal / noisy move generation, perft
  rules.h      game status: checkmate, stalemate, bare kings, repetition, 50-move rule
  notation.h   piece letters, move strings
src/           implementations of the above
tools/         perft.cpp, selfplay.cpp
tests/         unit_tests.cpp
validation/    independent reference generator + position sets (see validation/README.md)
```

Design notes:

* **Square = 0..124** (`z*25 + y*5 + x`). All movement geometry is precomputed once into
  read-only tables (`tables()`), so generation never bounds-checks. The same index will
  serve as the square part of the NNUE input features.
* **Make/unmake with an undo stack.** `Position` keeps the Zobrist key, king squares and
  halfmove clock in sync incrementally; `checkConsistency()` recomputes them from scratch.
  The undo stack doubles as the repetition history.
* **Legal moves = pseudo-legal moves filtered by make → "is my king attacked?" → unmake.**
  Simple and hard to get wrong. A faster pin-aware generator can replace it later and be
  checked against this one with perft.
* **`GenType::Noisy`** (captures + promotions) exists now for the quiescence search.
* **Own PRNG** instead of `<random>` distributions, which differ between standard
  libraries, so seeded experiments reproduce on any machine.

## Validation status

Reference perft values from the starting position (this generator):

| Depth | Nodes |
|-------|-------|
| 1 | 61 |
| 2 | 3,608 |
| 3 | 236,511 |
| 4 | 15,491,601 |

What has been checked:

* **Depth 1 by hand:** 61 = pawns 15 + knights 12 + queen 14 + bishops 13 + unicorns 7
  (rooks and king are blocked). Black also has 61 (the setup is point-symmetric).
* **Against an independently written reference generator** (`validation/`): identical
  counts on the starting position to depth 3 and on 595 positions from random games
  (including 150 chosen for checks and promotions) to depth 2 and 3. About 17M nodes,
  zero mismatches. See `validation/README.md` for how it was produced and how to re-run it.
* **Unit tests:** all passing (`unit_tests`).
* **Self-play:** thousands of random games with full make/unmake checks: all checks
  passed, and every game-end type occurred.
* **Clean** under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (GCC 11 and 13, Clang 18)
  and under AddressSanitizer + UBSan.

**Still to do (yours):** hand-check 5–10 positions (pawn captures both colours,
promotion, moves near check and mate) and add them to `tests/unit_tests.cpp`. That is
the only fully human-independent check. All the code here, including the reference
generator, is AI-written; say so in the write-up.

## Speed

About 13 million perft nodes per second (single thread, Release build). That's ample for
a first search; generating NNUE training data may later justify a pin-aware generator.
