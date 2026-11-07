# Move-generator validation

Checks that the engine's move generator (C++) matches an independently written one.

## Files

| File | What it is |
|------|------------|
| `ref_perft.py` | Reference move generator in plain Python (standard library only). |
| `positions_general.txt` | 445 positions sampled from 300 random games (seed 2027). |
| `positions_check_promotion.txt` | 150 positions from the same games where the side to move is in check or can promote. |

## How `ref_perft.py` was produced

A separate AI agent, started fresh with no access to this conversation, wrote it from
`RULES.md` and the position-format description alone. It was instructed to work only in
an empty directory and not to read the engine code, and confirmed that it complied.
That isolation was by instruction, not technically enforced (same machine). Both programs
come from the same underlying AI model, so a misreading of ambiguous wording in
`RULES.md` could in principle be shared. The hand-checked positions in
`tests/unit_tests.cpp` are the human-independent check.

The helper reported these points as ambiguous and resolved them as below. Both
programs agree on all of them:

* A pawn promotes only on level E **and** rank 5 (Black: level A and rank 1). A White pawn
  on rank 5 of a lower level does not promote and can still move up.
* Promotion is compulsory, with 5 choices (q r b n u): each promoting move counts as 5 moves.
* A pawn attacks only its four capture cells; its straight steps attack nothing.

## Re-running the comparison

Build the engine first (see the top-level README), then from the project root:

```sh
for f in validation/positions_general.txt validation/positions_check_promotion.txt; do
  ./build/perft 2 --batch < $f > engine.txt
  python3 validation/ref_perft.py perft 2 < $f > reference.txt
  diff engine.txt reference.txt && echo "$f: identical"
done
```

(With the Make build, use `./build-make/perft`. On Windows/Visual Studio, use
`build\Release\perft.exe`.)

To locate a disagreement, compare `divide` output for the first position that differs:

```sh
./build/perft 3 --text "<position>" --divide | grep ': ' | sort > e.txt
echo "<position>" | python3 validation/ref_perft.py divide 3 | grep ': ' | sort > r.txt
diff e.txt r.txt
```

Then play the move whose count differs and repeat one level shallower.

## Results (corrected starting position)

| Test | Positions | Depth | Nodes | Mismatches |
|------|-----------|-------|-------|------------|
| Starting position | 1 | 1–3 | 240,180 | 0 |
| General | 445 | 2 | 1,073,376 | 0 |
| Check / promotion | 150 | 2 | 181,765 | 0 |
| Check / promotion | 150 | 3 | 8,927,191 | 0 |
| General (first 40) | 40 | 3 | 6,796,928 | 0 |

About 17 million nodes in total with zero disagreements. An earlier run on a different
set of ~15M nodes (from the original, mis-transcribed setup) also showed zero disagreements.
