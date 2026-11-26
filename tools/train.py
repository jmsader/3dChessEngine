"""Train a learned evaluation for the Raumschach engine.

Skeleton to fill in: every TODO is yours. The steps are in the order a typical PyTorch
project runs them; read top to bottom.

    python tools/train.py data/Alpha-Beta+Ord+Q_d2_seed*.csv --model-kind linear

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
import numpy as np
import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset

PIECES = "PNBRUQK"  # piece letters in a fixed order -> 7 types
NUM_SQUARES = 125
NUM_FEATURES = (
    2 * len(PIECES) * NUM_SQUARES
)  # (own/opponent) x 7 types x 125 squares = 1750

# Training settings, edited here rather than given on the command line.
LEARNING_RATE = {"linear": 1e-2, "mlp": 1e-3}  # per model kind (tried on the 1 ms data)
EPOCHS = 15  # the best epoch (lowest validation loss) is the one exported
OPT_LAYER_OUTPUT_SIZES = (32, 16)


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
    p.add_argument("--batch-size", type=int, default=1024)
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
# Positions are stored compactly: instead of a 1750-long one-hot vector, each position keeps
# only the indices of its active features (one per piece, at most MAX_PIECES), padded with
# NUM_FEATURES. A batch is turned into one-hot vectors only when the model needs it
# (to_one_hot), so millions of positions fit in memory: ~100 bytes each instead of 7 KB.
MAX_PIECES = 40  # the start position has the most pieces, and captures only remove them

# Feature offset (owner * 7*125 + type * 125) of each piece letter, seen from White:
# uppercase = side to move ("own"), lowercase = opponent. -1 for an empty square.
CHAR_OFFSET = np.full(256, -1, dtype=np.int32)
for _type, _ch in enumerate(PIECES):
    CHAR_OFFSET[ord(_ch)] = 0 * len(PIECES) * NUM_SQUARES + _type * NUM_SQUARES
    CHAR_OFFSET[ord(_ch.lower())] = 1 * len(PIECES) * NUM_SQUARES + _type * NUM_SQUARES
SWAP_CASE = np.arange(256, dtype=np.uint8)  # byte -> the same letter in the other case
for _ch in PIECES:
    SWAP_CASE[ord(_ch)], SWAP_CASE[ord(_ch.lower())] = ord(_ch.lower()), ord(_ch)


def encode_indices(boards, stms):
    """Boards -> active feature indices, from the side to move's view, for many positions at once.

    boards: list of N board strings (125 chars); stms: N side-to-move letters ("w" / "b").
    Returns an (N, MAX_PIECES) int16 array: each row's feature indices, padded with NUM_FEATURES.

    Side-to-move view means the model always sees "my pieces" and "opponent pieces", never
    White/Black. When Black is to move:
      * swap colours: Black's pieces become "own", White's "opponent";
      * flip the board: Raumschach's start position is point-symmetric, so square sq maps to
        124 - sq (the unit test "start position symmetry" checks this).

    Feature index for a piece: owner * 7*125 + type * 125 + square
      owner 0 = side to move, 1 = opponent; type = PIECES.index(letter.upper()).
    The C++ engine (src/model.cpp) uses exactly the same encoding.
    """
    b = np.frombuffer("".join(boards).encode("ascii"), dtype=np.uint8)
    b = b.reshape(len(boards), NUM_SQUARES).copy()
    black = np.asarray(stms) == "b"
    b[black] = SWAP_CASE[b[black, ::-1]]  # flip (sq -> 124 - sq) and swap colours
    offsets = CHAR_OFFSET[b]  # (N, 125)
    features = np.where(offsets >= 0, offsets + np.arange(NUM_SQUARES), NUM_FEATURES)
    features.sort(axis=1)  # real features first, padding (NUM_FEATURES) last
    return features[:, :MAX_PIECES].astype(np.int16)


def to_one_hot(features):
    """(B, MAX_PIECES) feature indices -> (B, NUM_FEATURES) float32 one-hot rows."""
    x = torch.zeros(len(features), NUM_FEATURES + 1)  # the extra column absorbs the padding
    x.scatter_(1, features.long(), 1.0)
    return x[:, :NUM_FEATURES]


def load_data(files):
    """Read every CSV into NumPy arrays, one entry per position:
    features (N, MAX_PIECES), score (N,), result (N,), game_id (N,).

    Game numbers restart at 0 in each file, so game_id = file_index * 1,000,000 + game is
    unique across files - the train/validation split needs that.
    """
    parts = []
    for file_index, path in enumerate(files):
        df = pd.read_csv(path, dtype={"board": str, "stm": str})
        parts.append(
            {
                "features": encode_indices(df["board"].tolist(), df["stm"].to_numpy()),
                "score": df["score"].to_numpy(np.float32),
                "result": df["result"].to_numpy(np.float32),
                "game_id": file_index * 1_000_000 + df["game"].to_numpy(np.int64),
            }
        )
    return {key: np.concatenate([part[key] for part in parts]) for key in parts[0]}


def sigmoid(x):
    """1 / (1 + e^-x): maps any number (or NumPy array) to (0, 1)."""
    return 1.0 / (1.0 + np.exp(-x))


def make_label(score, result, res_w, k):
    """Target in [0, 1]: a mix of the game result and the search score as a win probability.

    sigmoid(score / k) turns centipawns into "probability of winning": 0 cp -> 0.5,
    +k cp -> about 0.73. A mix stays a valid probability, so BCE loss works.
    Works on single numbers and on whole NumPy arrays.
    """
    return res_w * result + (1 - res_w) * sigmoid(score / k)


class PositionDataset(Dataset):
    """The positions at `rows` (indices into the arrays from load_data), with their labels."""

    def __init__(self, data, rows, res_w, k):
        self.features = data["features"][rows]  # (N, MAX_PIECES) int16
        self.y = make_label(data["score"][rows], data["result"][rows], res_w, k).astype(
            np.float32
        )  # (N,)

    def __len__(self):
        return len(self.y)

    def __getitem__(self, r):
        # Position r: its feature indices (MAX_PIECES,) and its label (a scalar).
        return torch.from_numpy(self.features[r]), torch.tensor(self.y[r])

    def __getitems__(self, rows):
        # A whole batch at once (the DataLoader uses this when it exists): much faster than
        # fetching millions of positions one by one.
        rows = np.asarray(rows)
        return torch.from_numpy(self.features[rows]), torch.from_numpy(self.y[rows])


def one_hot_batch(batch):
    """DataLoader collate_fn: a batch from __getitems__ -> (one-hot inputs, labels)."""
    features, y = batch
    return to_one_hot(features), y


def split_by_game(game_id, val_fraction, seed):
    """Train/validation split by GAME, not by position. Returns two arrays of row indices.

    Positions from one game are highly correlated (and share the same result), so a
    position-level split leaks information and makes validation look better than it is.
    """
    games = sorted(set(game_id.tolist()))
    random.Random(seed).shuffle(games)  # reproducible
    n_val = int(len(games) * val_fraction)
    is_val = np.isin(game_id, games[:n_val])
    return np.flatnonzero(~is_val), np.flatnonzero(is_val)


# --------------------------------------------------------------------------- 3. model
def build_model(kind):
    """The model outputs one number per position: a logit, i.e. eval_cp / K.

    linear: nn.Linear(NUM_FEATURES, 1)  -> its weights * K are piece-square values in cp
    mlp:    Linear(NUM_FEATURES, h1) -> ReLU -> Linear(h1, h2) -> ReLU -> Linear(h2, 1),
            with (h1, h2) = OPT_LAYER_OUTPUT_SIZES
    """
    if kind == "linear":
        return nn.Sequential(nn.Linear(NUM_FEATURES, 1))
    elif kind == "mlp":
        return nn.Sequential(
            nn.Linear(NUM_FEATURES, OPT_LAYER_OUTPUT_SIZES[0]),
            nn.ReLU(),
            nn.Linear(OPT_LAYER_OUTPUT_SIZES[0], OPT_LAYER_OUTPUT_SIZES[1]),
            nn.ReLU(),
            nn.Linear(OPT_LAYER_OUTPUT_SIZES[1], 1),
        )
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
    """weights/<data config>_<model>.txt, e.g. weights/Alpha-Beta+Ord+Q_d2_linear.txt,
    so every engine x time control (and model) gets its own weights file."""
    stem = Path(files[0]).stem  # e.g. Alpha-Beta+Ord+Q_d2_seed1
    config = stem.rsplit("_seed", 1)[0]  # drop the seed: all seeds go into one model
    return Path(f"{config}_{kind}.txt")


# --------------------------------------------------------------------------- main
def main():
    args = parse_args()
    set_seed(args.seed)

    data = load_data(args.files)
    train_rows, val_rows = split_by_game(data["game_id"], args.val_fraction, args.seed)
    train_set = PositionDataset(data, train_rows, args.res_w, args.k)
    val_set = PositionDataset(data, val_rows, args.res_w, args.k)
    del data  # the datasets keep their own copies
    print(f"{len(train_set)} training / {len(val_set)} validation positions")

    train_loader = DataLoader(
        train_set, batch_size=args.batch_size, shuffle=True, collate_fn=one_hot_batch
    )
    val_loader = DataLoader(val_set, batch_size=args.batch_size, collate_fn=one_hot_batch)

    model = build_model(args.model_kind)
    loss_fn = (
        nn.BCEWithLogitsLoss()
    )  # targets are probabilities in [0, 1]; model gives logits
    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=LEARNING_RATE[args.model_kind],
        weight_decay=args.weight_decay,
    )

    out_dir = Path(args.out_dir)
    out_dir.mkdir(exist_ok=True)
    final_model_path = out_dir / "final_model.pt"
    best_val = float("inf")
    for epoch in range(1, EPOCHS + 1):
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
