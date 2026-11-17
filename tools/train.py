"""Train a learned evaluation for the Raumschach engine.

Skeleton to fill in: every TODO is yours. The steps are in the order a typical PyTorch
project runs them; read top to bottom.

    python tools/train.py data/Alpha-Beta+Ord+Q_1ms_seed*.csv --model-kind linear

Input files: CSVs from tools/gendata (one row per position):
    board,stm,score,result,ply,game
    board   125 chars, square index = level*25 + rank*5 + file; PNBRUQK White, pnbruqk Black, '.' empty
    stm     side to move, w or b
    score   engine search score, centipawns, side to move's view
    result  game result for the side to move: 1 / 0.5 / 0
"""

import argparse
import random
from pathlib import Path
import csv
import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset
import math

PIECES = "PNBRUQK"  # piece letters in a fixed order -> 7 types
NUM_SQUARES = 125
NUM_FEATURES = (
    2 * len(PIECES) * NUM_SQUARES
)  # (own/opponent) x 7 types x 125 squares = 1750


# --------------------------------------------------------------------------- 1. config
def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("files", nargs="+", help="CSV files from gendata")
    p.add_argument("--model-kind", choices=["linear", "mlp"], default="linear")
    p.add_argument(
        "--res_w",
        type=float,
        default=0.5,
        help="label mix: res_w*result + (1-res_w)*sigmoid(score/K)",
    )
    p.add_argument(
        "--k",
        type=float,
        default=400.0,
        help="score scale K in centipawns (see make_label)",
    )
    p.add_argument("--epochs", type=int, default=10)
    p.add_argument("--batch-size", type=int, default=1024)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument(
        "--weight-decay",
        type=float,
        default=1e-4,
        help="AdamW weight decay: pulls weights toward 0 (0 = none, plain Adam)",
    )
    p.add_argument("--val-fraction", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--out-dir", default="weights")
    return p.parse_args()


def set_seed(seed):
    """Make runs reproducible: seed Python, NumPy and PyTorch."""
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)


# --------------------------------------------------------------------------- 2. data
def load_rows(files):
    """Read every CSV into one list of rows (dicts or tuples, your choice).

    Tip: game numbers restart at 0 in each file, so make a game id that is unique across
    files (e.g. (file index, game)) - you need it for the train/val split.
    """
    rows = []
    for file_index, path in enumerate(files):
        with open(path) as f:
            for row in csv.DictReader(f):
                rows.append(
                    {
                        "board": row["board"],
                        "stm": row["stm"],
                        "score": int(row["score"]),
                        "result": float(row["result"]),
                        "game_id": (
                            file_index,
                            int(row["game"]),
                        ),  # games restart at 0 in each file
                    }
                )
    return rows


def encode(board, stm):
    """Board string -> float32 vector of NUM_FEATURES (one-hot), from the side to move's view.

    Side-to-move view means the model always sees "my pieces" and "opponent pieces", never
    White/Black. When Black is to move:
      * swap colours: Black's pieces become "own", White's "opponent";
      * flip the board: Raumschach's start position is point-symmetric, so square sq maps to
        124 - sq (the unit test "start position symmetry" checks this).

    Feature index for a piece: owner * 7*125 + type * 125 + square
      owner 0 = side to move, 1 = opponent; type = PIECES.index(letter.upper()).
    """
    x = np.zeros(NUM_FEATURES, dtype=np.float32)
    black_to_move = stm == "b"
    for sq, ch in enumerate(board):
        if ch == ".":
            continue
        is_white = ch.isupper()
        owner = 0 if is_white != black_to_move else 1  # 0 = side to move, 1 = opponent
        square = (
            NUM_SQUARES - 1 - sq if black_to_move else sq
        )  # flip board to train equally
        piece_type = PIECES.index(ch.upper())
        x[owner * len(PIECES) * NUM_SQUARES + piece_type * NUM_SQUARES + square] = (
            1.0  # 3d indexing
        )

    return x


def sigmoid(x):
    """1 / (1 + e^-x): maps any number to (0, 1). (The math module has no sigmoid.)"""
    return 1.0 / (1.0 + math.exp(-x))


def make_label(score, result, res_w, k):
    """Target in [0, 1]: a mix of the game result and the search score as a win probability.

    sigmoid(score / k) turns centipawns into "probability of winning": 0 cp -> 0.5,
    +k cp -> about 0.73. A mix stays a valid probability, so BCE loss works.
    """
    return res_w * result + (1 - res_w) * sigmoid(score / k)


class PositionDataset(Dataset):
    """Pre-encode everything once in __init__ (fast), then index into the arrays."""

    def __init__(self, rows, res_w, k):
        # One row per position: x is the encoded board, y its label.
        self.x = np.stack(
            [encode(r["board"], r["stm"]) for r in rows]
        )  # (N, NUM_FEATURES)
        self.y = np.array(
            [make_label(r["score"], r["result"], res_w, k) for r in rows],
            dtype=np.float32,
        )  # (N,)

    def __len__(self):
        return len(self.y)

    def __getitem__(self, r):
        # Position r as tensors: its encoded board (1750,) and its label (a scalar).
        x = torch.from_numpy(self.x[r])  # no copy: shares memory with self.x
        y = torch.tensor(self.y[r])  # 0-dim float32 tensor

        return x, y


def split_by_game(rows, val_fraction, seed):
    """Train/validation split by GAME, not by position.

    Positions from one game are highly correlated (and share the same result), so a
    position-level split leaks information and makes validation look better than it is.
    """
    game_ids = sorted({r["game_id"] for r in rows})
    random.Random(seed).shuffle(game_ids)  # reproducible
    n_val = int(len(game_ids) * val_fraction)
    val_games = set(game_ids[:n_val])

    train_rows = [r for r in rows if r["game_id"] not in val_games]
    val_rows = [r for r in rows if r["game_id"] in val_games]
    return train_rows, val_rows


# --------------------------------------------------------------------------- 3. model
def build_model(kind):
    """The model outputs one number per position: a logit, i.e. eval_cp / K.

    linear: nn.Linear(NUM_FEATURES, 1)  -> its weights * K are piece-square values in cp
    mlp:    Linear(NUM_FEATURES, 64) -> ReLU -> Linear(64, 32) -> ReLU -> Linear(32, 1)
    """
    if kind == "linear":
        return nn.Sequential(nn.Linear(NUM_FEATURES, 1))
    else:
        raise ValueError(f"unknown model kind: {kind}!")


# --------------------------------------------------------------------------- 4. train / evaluate
def train_one_epoch(model, train_loader, loss_fn, optimizer):
    """Standard loop: model.train(); for each batch: zero_grad, forward, loss, backward, step.
    Return the average loss over the epoch."""
    model.train()  # may not matter for linear
    total_loss, total_ex = 0.0, 0
    for x, y in train_loader:  # x: (B, NUM_FEATURES), y: (B,)
        optimizer.zero_grad()  # clear the previous batch's gradients
        pred = model(x).squeeze(1)  # squeze (B, 1) -> (B,), to match y
        loss = loss_fn(pred, y)  # average loss over the batch
        loss.backward()  # gradients of the loss for every weight
        optimizer.step()  # update the weights using those gradients
        total_loss += loss.item() * len(y)  # .item(): tensor -> Python float
        total_ex += len(y)

    return total_loss / total_ex


@torch.no_grad()
def evaluate(model, val_loader, loss_fn):
    """model.eval(); average loss over the loader, no gradients."""
    model.eval()  # may not matter for linear
    total_loss, total_ex = 0.0, 0
    for x, y in val_loader:
        pred = model(x).squeeze(1)
        loss = loss_fn(pred, y)
        total_loss += loss.item() * len(y)
        total_ex += len(y)

    return total_loss / total_ex


# --------------------------------------------------------------------------- 5. export
def export_weights(model, k, path):
    """Write the weights in a plain text format the C++ engine can read.

    Works for any stack of nn.Linear layers with ReLU between them, so the same format
    covers the linear model (1 layer) and the MLP (3 layers). Only the LAST layer is
    multiplied by k: that turns the final output into centipawns, so the engine's
    evaluation stays in the same units as eval_material. (Scaling an earlier layer
    would be wrong, because ReLU sits in between.)

    File format, one number per line:
        number of layers L
        then, for each layer in order:
            inputs  outputs           (one line, two numbers)
            the biases                (outputs lines)
            the weights, row by row   (outputs * inputs lines: all weights of output 0,
                                       then output 1, ...; within a row, input order is
                                       the feature index owner*7*125 + type*125 + square)
    For the linear model: 1 / "1750 1" / the bias / the 1750 piece-square values.
    """
    layers = [l for l in model if isinstance(l, nn.Linear)]

    path.parent.mkdir(exist_ok=True)
    with open(path, "w") as f:
        for i, layer in enumerate(layers):
            scale = (
                k if i == len(layers) - 1 else 1.0
            )  # using k for the scale works out with logit assumption (to centipawns by time of infer output)
            weights = layer.weight.detach().cpu().numpy() * scale  # (outputs, inputs)
            biases = layer.bias.detach().cpu().numpy() * scale  # (outputs,)
            f.write(f"{layer.in_features} {layer.out_features}\n")
            for b in biases:
                f.write(f"{b:.5f}\n")
            for w in weights.reshape(-1):  # row by row
                f.write(f"{w:.5f}\n")


def output_name(files, kind):
    """weights/<data config>_<model>.txt, e.g. weights/Alpha-Beta+Ord+Q_1ms_linear.txt,
    so every engine x time control (and model) gets its own weights file."""
    stem = Path(files[0]).stem  # e.g. Alpha-Beta+Ord+Q_1ms_seed1
    config = stem.rsplit("_seed", 1)[0]  # drop the seed: all seeds go into one model
    return Path(f"{config}_{kind}.txt")


# --------------------------------------------------------------------------- main
def main():
    args = parse_args()
    set_seed(args.seed)

    rows = load_rows(args.files)
    train_rows, val_rows = split_by_game(rows, args.val_fraction, args.seed)
    train_set = PositionDataset(train_rows, args.res_w, args.k)
    val_set = PositionDataset(val_rows, args.res_w, args.k)
    print(f"{len(train_set)} training / {len(val_set)} validation positions")

    train_loader = DataLoader(train_set, batch_size=args.batch_size, shuffle=True)
    val_loader = DataLoader(val_set, batch_size=args.batch_size)

    model = build_model(args.model_kind)
    loss_fn = (
        nn.BCEWithLogitsLoss()
    )  # targets are probabilities in [0, 1]; model gives logits
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )

    out_dir = Path(args.out_dir)
    out_dir.mkdir(exist_ok=True)
    final_model_path = out_dir / "final_model.pt"
    best_val = float("inf")
    for epoch in range(1, args.epochs + 1):
        train_loss = train_one_epoch(model, train_loader, loss_fn, optimizer)
        val_loss = evaluate(model, val_loader, loss_fn)
        print(f"epoch {epoch}: train {train_loss:.3f}  val {val_loss:.3f}")
        if val_loss < best_val:
            best_val = val_loss
            torch.save(model.state_dict(), final_model_path)

    model.load_state_dict(torch.load(final_model_path))
    final_weights_path = out_dir / output_name(args.files, args.model_kind)
    export_weights(model, args.k, final_weights_path)


if __name__ == "__main__":
    main()
