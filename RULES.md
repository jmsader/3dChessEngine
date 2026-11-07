# Raumschach ruleset used by this project

Source: the Chess Variant Pages description of Raumschach (Ferdinand Maack, 1907),
<https://www.chessvariants.com/3d.dir/3d5.html>. Where that page is silent or disputed,
the choice made here is marked **[decision]**. Any change to this file must be matched
in the code (`include/raumschach/config.h`, `src/geometry.cpp`) and in the write-up.

## Board and coordinates

A 5 × 5 × 5 cube of 125 cells.

| Axis | Name  | Values | Code |
|------|-------|--------|------|
| x    | file  | a–e    | 0–4  |
| y    | rank  | 1–5    | 0–4  |
| z    | level | A–E    | 0–4  |

A cell is named level + file + rank, e.g. `Ac2` = level A, file c, rank 2 = (x 2, y 1, z 0).
`Aa1` is the front-left-bottom corner; `Ee5` is the opposite corner. Level A is White's
home level, level E is Black's.

## Starting position

| Level | Contents |
|-------|----------|
| A | White: Rook Aa1, Knight Ab1, King Ac1, Knight Ad1, Rook Ae1; Pawns Aa2–Ae2 |
| B | White: Bishop Ba1, Unicorn Bb1, Queen Bc1, Bishop Bd1, Unicorn Be1; Pawns Ba2–Be2 |
| C | empty |
| D | Black: Unicorn Da5, Bishop Db5, Queen Dc5, Unicorn Dd5, Bishop De5; Pawns Da4–De4 |
| E | Black: Rook Ea5, Knight Eb5, King Ec5, Knight Ed5, Rook Ee5; Pawns Ea4–Ee4 |

White moves first. Each side has 20 pieces. Black's army is White's reflected through
the centre of the cube (`Ba1` ↔ `De5`, `Bb1` ↔ `Dd5`, …).

**Correction to the source:** the Chess Variant Pages text and diagram place Black's
bishops on `Da5`/`Dd5` and unicorns on `Db5`/`De5`. That is a known error, pointed out
in comments on the page itself and inconsistent with Wikipedia, which puts the unicorns on
`Bb1`, `Be1`, `Da5` and `Dd5`. This project uses the corrected, point-symmetric setup above.

## Piece movement

A direction is a vector (dx, dy, dz) with each component in {−1, 0, +1}.

| Piece   | Moves |
|---------|-------|
| Rook    | Any distance along one axis: exactly one non-zero component (6 directions). |
| Bishop  | Any distance along a 2D diagonal: exactly two non-zero components (12 directions). |
| Unicorn | Any distance along a 3D diagonal: all three components non-zero (8 directions). |
| Queen   | Rook + Bishop + Unicorn (26 directions). |
| King    | One step in any of the 26 directions. |
| Knight  | A jump by any permutation of (±2, ±1, 0) (24 jumps); it jumps over pieces. |

Sliders stop at the first occupied cell, which they may capture if it holds an enemy piece.
From the centre `Cc3` on an empty board: rook 12, bishop 24, unicorn 16, queen 52,
king 26, knight 24 moves.

## Pawns

"Forward" is +y for White and −y for Black. "Vertical" is +z (up) for White and −z (down) for Black.

* **Non-capturing moves:** one step forward, or one step vertically, onto an empty cell.
* **Captures:** one step sideways (±x) combined with one step forward, or one step
  sideways combined with one step vertically. For White: (±1, +1, 0) and (±1, 0, +1).
  Example: a White pawn on `Ac2` may move to `Ac3` or `Bc2` and capture on `Ab3`, `Ad3`,
  `Bb2` or `Bd2`.
* **[decision]** The forward-and-vertical diagonal (White pawn on `Ac2` capturing on
  `Bc3`) is **not** a capture. The source's author rejects it; Dickins allows it.
* No two-step first move, therefore no en passant.

## Promotion

* **[decision]** White promotes on reaching **level E, rank 5** (`Ea5`–`Ee5`); Black on
  reaching **level A, rank 1** (`Aa1`–`Ae1`). This follows the CECV / Moeser reading. The
  source page's own wording is ambiguous and disputed in its comments.
* **[decision]** A pawn may promote to queen, rook, bishop, knight or unicorn
  (`config::kQueenOnlyPromotion = false`). The source does not specify.

## Check and game end

* A move may not leave the mover's own king attacked. Kings are never captured.
* **[decision]** No castling (the source does not mention it).
* **Checkmate:** side to move is in check and has no legal move → that side loses.
* **Stalemate [decision]:** side to move is not in check and has no legal move → draw.
* **Threefold repetition [decision]:** the same placement with the same side to move
  occurs a third time → draw.
* **50-move rule [decision]:** 100 consecutive half-moves with no capture and no pawn
  move → draw (`config::kFiftyMoveHalfmoves`).
* **Bare kings [decision]:** when only the two kings remain → draw
  (`config::kBareKingsDraw`). Mate is impossible, so this only ends hopeless games sooner.
* Checkmate or stalemate on the move that also triggers a draw rule takes precedence.
* **[decision]** No other insufficient-material rule. Which material can force mate in 3D
  is not established; the 50-move rule ends such games.
* **[decision]** No move cap.
