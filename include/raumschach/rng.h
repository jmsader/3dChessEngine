// Small deterministic PRNG (xoshiro256**, seeded with splitmix64).
//
// std::mt19937 is portable, but std::uniform_int_distribution is not: different
// standard libraries produce different sequences. Experiments must be reproducible
// across machines, so the engine uses its own generator and its own range reduction.
#pragma once

#include <cassert>
#include <cstdint>

namespace rs {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        for (auto& word : s_) word = splitmix64(seed);
    }

    std::uint64_t next() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform integer in [0, n), n > 0. Rejection sampling, so no modulo bias.
    std::uint64_t below(std::uint64_t n) {
        assert(n > 0);
        const std::uint64_t limit = ~std::uint64_t(0) - (~std::uint64_t(0) % n);
        std::uint64_t r;
        do {
            r = next();
        } while (r >= limit);
        return r % n;
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static std::uint64_t splitmix64(std::uint64_t& state) {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t s_[4]{};
};

}  // namespace rs
