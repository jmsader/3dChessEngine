#!/usr/bin/env python3
"""
Independent, deliberately plain reference move generator for Raumschach
(5x5x5 three-dimensional chess).  Used only to cross-check move counts.

Usage:
    python3 ref_perft.py perft DEPTH    < positions (one per line)
    python3 ref_perft.py divide DEPTH   < one position

Coordinates: x = file (a-e -> 0-4), y = rank (1-5 -> 0-4), z = level (A-E -> 0-4).
A cell is stored at list index  x + 5*y + 25*z.
The board is a list of 125 entries, each None (empty) or a one-character
piece code: K Q R B N U P for White, lowercase for Black.
"""

import sys

SIZE = 5
LEVEL_LETTERS = "ABCDE"
FILE_LETTERS = "abcde"
RANK_LETTERS = "12345"

PROMOTION_CHOICES = "qrbnu"


# ----------------------------------------------------------------------------
# Geometry
# ----------------------------------------------------------------------------

def index_of(x, y, z):
    return x + SIZE * y + SIZE * SIZE * z


def coords_of(i):
    return i % SIZE, (i // SIZE) % SIZE, i // (SIZE * SIZE)


def on_board(x, y, z):
    return 0 <= x < SIZE and 0 <= y < SIZE and 0 <= z < SIZE


def cell_name(i):
    x, y, z = coords_of(i)
    return LEVEL_LETTERS[z] + FILE_LETTERS[x] + RANK_LETTERS[y]


def parse_cell_name(name):
    z = LEVEL_LETTERS.index(name[0])
    x = FILE_LETTERS.index(name[1])
    y = RANK_LETTERS.index(name[2])
    return index_of(x, y, z)


def count_nonzero(vec):
    n = 0
    for c in vec:
        if c != 0:
            n += 1
    return n


ALL_DIRECTIONS = []
for _dx in (-1, 0, 1):
    for _dy in (-1, 0, 1):
        for _dz in (-1, 0, 1):
            if (_dx, _dy, _dz) != (0, 0, 0):
                ALL_DIRECTIONS.append((_dx, _dy, _dz))

ROOK_DIRECTIONS = [d for d in ALL_DIRECTIONS if count_nonzero(d) == 1]
BISHOP_DIRECTIONS = [d for d in ALL_DIRECTIONS if count_nonzero(d) == 2]
UNICORN_DIRECTIONS = [d for d in ALL_DIRECTIONS if count_nonzero(d) == 3]
QUEEN_DIRECTIONS = list(ALL_DIRECTIONS)
KING_STEPS = list(ALL_DIRECTIONS)

# Knight: every permutation of (+-2, +-1, 0).
_knight_set = set()
for _a in (2, -2):
    for _b in (1, -1):
        base = (_a, _b, 0)
        for perm in ((0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0)):
            _knight_set.add((base[perm[0]], base[perm[1]], base[perm[2]]))
KNIGHT_JUMPS = sorted(_knight_set)

assert len(ALL_DIRECTIONS) == 26
assert len(ROOK_DIRECTIONS) == 6
assert len(BISHOP_DIRECTIONS) == 12
assert len(UNICORN_DIRECTIONS) == 8
assert len(KNIGHT_JUMPS) == 24

SLIDER_DIRECTIONS = {
    "R": ROOK_DIRECTIONS,
    "B": BISHOP_DIRECTIONS,
    "U": UNICORN_DIRECTIONS,
    "Q": QUEEN_DIRECTIONS,
}


# ----------------------------------------------------------------------------
# Pieces and colours
# ----------------------------------------------------------------------------

def colour_of(piece):
    return "w" if piece.isupper() else "b"


def other_colour(side):
    return "b" if side == "w" else "w"


def pawn_forward(side):
    """+1 for White (towards rank 5 / level E), -1 for Black."""
    return 1 if side == "w" else -1


def pawn_quiet_offsets(side):
    f = pawn_forward(side)
    return [(0, f, 0), (0, 0, f)]          # one step forward, one step vertical


def pawn_capture_offsets(side):
    f = pawn_forward(side)
    return [(1, f, 0), (-1, f, 0),          # sideways + forward
            (1, 0, f), (-1, 0, f)]          # sideways + vertical


def is_promotion_cell(side, i):
    x, y, z = coords_of(i)
    if side == "w":
        return y == SIZE - 1 and z == SIZE - 1     # level E, rank 5
    return y == 0 and z == 0                         # level A, rank 1


# ----------------------------------------------------------------------------
# Position parsing / formatting
# ----------------------------------------------------------------------------

def parse_position(line):
    fields = line.split()
    if len(fields) < 2:
        raise ValueError("position needs at least a board and a side to move: %r" % line)
    board_text, side = fields[0], fields[1]
    if side not in ("w", "b"):
        raise ValueError("bad side to move %r" % side)
    levels = board_text.split("|")
    if len(levels) != SIZE:
        raise ValueError("expected 5 levels, got %d" % len(levels))
    board = [None] * (SIZE ** 3)
    for z, level_text in enumerate(levels):
        ranks = level_text.split("/")
        if len(ranks) != SIZE:
            raise ValueError("expected 5 ranks in level %s" % LEVEL_LETTERS[z])
        for y, rank_text in enumerate(ranks):
            x = 0
            for ch in rank_text:
                if ch.isdigit():
                    x += int(ch)
                elif ch.upper() in "KQRBNUP":
                    if x >= SIZE:
                        raise ValueError("rank too long: %r" % rank_text)
                    board[index_of(x, y, z)] = ch
                    x += 1
                else:
                    raise ValueError("bad character %r" % ch)
            if x != SIZE:
                raise ValueError("rank %r does not have 5 cells" % rank_text)
    return board, side


def format_position(board, side):
    levels = []
    for z in range(SIZE):
        ranks = []
        for y in range(SIZE):
            text = ""
            empties = 0
            for x in range(SIZE):
                p = board[index_of(x, y, z)]
                if p is None:
                    empties += 1
                else:
                    if empties:
                        text += str(empties)
                        empties = 0
                    text += p
            if empties:
                text += str(empties)
            ranks.append(text)
        levels.append("/".join(ranks))
    return "|".join(levels) + " " + side


# ----------------------------------------------------------------------------
# Move generation
# ----------------------------------------------------------------------------
# A move is a tuple (from_index, to_index, promotion) where promotion is None
# or one of the lowercase letters in PROMOTION_CHOICES.

def move_string(move):
    frm, to, promo = move
    return cell_name(frm) + cell_name(to) + (promo if promo else "")


def pseudo_legal_moves(board, side):
    moves = []
    for i in range(SIZE ** 3):
        piece = board[i]
        if piece is None or colour_of(piece) != side:
            continue
        x, y, z = coords_of(i)
        kind = piece.upper()

        if kind in SLIDER_DIRECTIONS:
            for (dx, dy, dz) in SLIDER_DIRECTIONS[kind]:
                cx, cy, cz = x + dx, y + dy, z + dz
                while on_board(cx, cy, cz):
                    j = index_of(cx, cy, cz)
                    target = board[j]
                    if target is None:
                        moves.append((i, j, None))
                    else:
                        if colour_of(target) != side:
                            moves.append((i, j, None))
                        break
                    cx, cy, cz = cx + dx, cy + dy, cz + dz

        elif kind == "K" or kind == "N":
            steps = KING_STEPS if kind == "K" else KNIGHT_JUMPS
            for (dx, dy, dz) in steps:
                cx, cy, cz = x + dx, y + dy, z + dz
                if not on_board(cx, cy, cz):
                    continue
                j = index_of(cx, cy, cz)
                target = board[j]
                if target is None or colour_of(target) != side:
                    moves.append((i, j, None))

        elif kind == "P":
            destinations = []
            for (dx, dy, dz) in pawn_quiet_offsets(side):
                cx, cy, cz = x + dx, y + dy, z + dz
                if on_board(cx, cy, cz) and board[index_of(cx, cy, cz)] is None:
                    destinations.append(index_of(cx, cy, cz))
            for (dx, dy, dz) in pawn_capture_offsets(side):
                cx, cy, cz = x + dx, y + dy, z + dz
                if not on_board(cx, cy, cz):
                    continue
                target = board[index_of(cx, cy, cz)]
                if target is not None and colour_of(target) != side:
                    destinations.append(index_of(cx, cy, cz))
            for j in destinations:
                if is_promotion_cell(side, j):
                    for promo in PROMOTION_CHOICES:
                        moves.append((i, j, promo))
                else:
                    moves.append((i, j, None))

        else:
            raise ValueError("unknown piece %r" % piece)
    return moves


def make_move(board, move, side):
    """Return a NEW board with the move played (the input board is untouched)."""
    frm, to, promo = move
    new_board = list(board)
    piece = new_board[frm]
    new_board[frm] = None
    if promo is not None:
        piece = promo.upper() if side == "w" else promo.lower()
    new_board[to] = piece
    return new_board


# ----------------------------------------------------------------------------
# Attack detection
# ----------------------------------------------------------------------------

def is_square_attacked(board, square, by_side):
    """Is `square` attacked by any piece of colour `by_side`?

    Looks outwards from the square: along each of the 26 lines for sliders
    and an adjacent king, at the 24 knight jumps, and at the cells from which
    an enemy pawn's capture would land on `square`."""
    x, y, z = coords_of(square)

    # Sliders and king, looking along every one of the 26 directions.
    for d in ALL_DIRECTIONS:
        dx, dy, dz = d
        kind_of_line = count_nonzero(d)   # 1 rook-line, 2 bishop-line, 3 unicorn-line
        cx, cy, cz = x + dx, y + dy, z + dz
        distance = 1
        while on_board(cx, cy, cz):
            p = board[index_of(cx, cy, cz)]
            if p is not None:
                if colour_of(p) == by_side:
                    kind = p.upper()
                    if kind == "Q":
                        return True
                    if kind == "R" and kind_of_line == 1:
                        return True
                    if kind == "B" and kind_of_line == 2:
                        return True
                    if kind == "U" and kind_of_line == 3:
                        return True
                    if kind == "K" and distance == 1:
                        return True
                break
            cx, cy, cz = cx + dx, cy + dy, cz + dz
            distance += 1

    # Knights.
    for (dx, dy, dz) in KNIGHT_JUMPS:
        cx, cy, cz = x + dx, y + dy, z + dz
        if on_board(cx, cy, cz):
            p = board[index_of(cx, cy, cz)]
            if p is not None and colour_of(p) == by_side and p.upper() == "N":
                return True

    # Pawns: an attacking pawn stands at  square - capture_offset.
    for (dx, dy, dz) in pawn_capture_offsets(by_side):
        cx, cy, cz = x - dx, y - dy, z - dz
        if on_board(cx, cy, cz):
            p = board[index_of(cx, cy, cz)]
            if p is not None and colour_of(p) == by_side and p.upper() == "P":
                return True

    return False


def is_square_attacked_slow(board, square, by_side):
    """Second, independent attack test used only for self-checking:
    enumerate every attacker piece and every cell it attacks."""
    for i in range(SIZE ** 3):
        p = board[i]
        if p is None or colour_of(p) != by_side:
            continue
        x, y, z = coords_of(i)
        kind = p.upper()
        if kind in SLIDER_DIRECTIONS:
            for (dx, dy, dz) in SLIDER_DIRECTIONS[kind]:
                cx, cy, cz = x + dx, y + dy, z + dz
                while on_board(cx, cy, cz):
                    j = index_of(cx, cy, cz)
                    if j == square:
                        return True
                    if board[j] is not None:
                        break
                    cx, cy, cz = cx + dx, cy + dy, cz + dz
        else:
            if kind == "K":
                offsets = KING_STEPS
            elif kind == "N":
                offsets = KNIGHT_JUMPS
            else:
                offsets = pawn_capture_offsets(by_side)
            for (dx, dy, dz) in offsets:
                cx, cy, cz = x + dx, y + dy, z + dz
                if on_board(cx, cy, cz) and index_of(cx, cy, cz) == square:
                    return True
    return False


def find_king(board, side):
    wanted = "K" if side == "w" else "k"
    for i in range(SIZE ** 3):
        if board[i] == wanted:
            return i
    return None


def in_check(board, side):
    k = find_king(board, side)
    if k is None:        # only possible in hand-made test positions
        return False
    return is_square_attacked(board, k, other_colour(side))


def legal_moves(board, side):
    result = []
    for move in pseudo_legal_moves(board, side):
        after = make_move(board, move, side)
        if not in_check(after, side):
            result.append(move)
    return result


# ----------------------------------------------------------------------------
# Perft
# ----------------------------------------------------------------------------

def perft(board, side, depth):
    if depth == 0:
        return 1
    moves = legal_moves(board, side)
    if depth == 1:
        return len(moves)
    total = 0
    opponent = other_colour(side)
    for move in moves:
        total += perft(make_move(board, move, side), opponent, depth - 1)
    return total


def divide(board, side, depth):
    rows = []
    opponent = other_colour(side)
    for move in legal_moves(board, side):
        count = perft(make_move(board, move, side), opponent, depth - 1)
        rows.append((move_string(move), count))
    rows.sort()
    return rows


# ----------------------------------------------------------------------------
# Command line
# ----------------------------------------------------------------------------

def main(argv):
    if len(argv) != 3 or argv[1] not in ("perft", "divide"):
        sys.stderr.write("usage: ref_perft.py perft|divide DEPTH  < positions\n")
        return 2
    mode = argv[1]
    depth = int(argv[2])
    if depth < 0:
        sys.stderr.write("DEPTH must be >= 0\n")
        return 2
    lines = [ln.strip() for ln in sys.stdin.read().splitlines()]
    lines = [ln for ln in lines if ln]

    if mode == "perft":
        for ln in lines:
            board, side = parse_position(ln)
            print(perft(board, side, depth))
            sys.stdout.flush()
    else:
        if not lines:
            sys.stderr.write("no position given\n")
            return 2
        if depth < 1:
            sys.stderr.write("divide needs DEPTH >= 1\n")
            return 2
        board, side = parse_position(lines[0])
        total = 0
        for text, count in divide(board, side, depth):
            print("%s: %d" % (text, count))
            total += count
        print("total %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
