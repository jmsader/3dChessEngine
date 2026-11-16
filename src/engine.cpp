#include "raumschach/engine.h"
#include "raumschach/position.h"
#include "raumschach/types.h"
#include <cstdint>
#include <map>

namespace rs {

namespace {
std::map<rs::PieceType, int> piece_cpawns_map = {
    {Pawn, 100},   {Unicorn, 140}, {Rook, 245}, {Bishop, 295},
    {Knight, 320}, {Queen, 980},   {King, 0}};
// constexpr it.. but cant use dict. Determined from howard36 (2026)
} // namespace

Engine::Engine(std::string name) : name(name) {}

int Engine::piece_cpawns(PieceType pt) { return piece_cpawns_map.at(pt); }

int Engine::eval_material(const Position &pos) {
  int cpawns = 0;
  const Color turn = pos.sideToMove();
  for (Square sq_idx = 0; sq_idx < kNumSquares; ++sq_idx) {
    Piece piece = pos.pieceAt(sq_idx);
    PieceType piece_type = typeOf(piece);
    if (piece_type != NoPieceType) {
      int cpawns_here = piece_cpawns(piece_type);
      cpawns += (turn == colorOf(piece)) ? cpawns_here : -cpawns_here;
    }
  }

  return cpawns;
}

bool Engine::is_mate_cpawns(int cp) {
  const int a = cp < 0 ? -cp : cp; // |s| (std::abs isn't constexpr in C++17)
  return a >= MATE_CPAWNS - MAX_PLY && a <= MATE_CPAWNS;
}

void Engine::seed(std::uint64_t s) { rng.reseed(s); }

} // namespace rs