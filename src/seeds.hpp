#pragma once

#ifndef BUILD_XOR_SALT
// Default fallback salt (override this in your project preprocessor definitions)
#define BUILD_XOR_SALT 0xDEADBEefu
#endif

namespace seeds
{
    // Improved compile-time mixing that includes a build-time salt.
    static INLINE constexpr std::uint32_t const_xs32_from_seed(std::uint32_t seed, int add = 0)
    {
        constexpr std::uint32_t salt = static_cast<std::uint32_t>(BUILD_XOR_SALT);

        // original xorshift
        seed ^= seed << (13 + add);
        seed ^= seed >> (17 + add);
        seed ^= seed << (15 + add);

        // mix with build-time salt using a small non-linear scramble (Knuth-like mix)
        seed ^= salt + 0x9e3779b9u + (seed << 6) + (seed >> 2);

        // avalanche
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;

        return seed;
    }

    // Runtime variant (same mixing) - keep parity with constexpr version
    static INLINE std::uint32_t xs32_from_seed(std::uint32_t seed, int add = 0)
    {
        std::uint32_t salt = static_cast<std::uint32_t>(BUILD_XOR_SALT);

        seed ^= seed << (13 + add);
        seed ^= seed >> (17 + add);
        seed ^= seed << (15 + add);

        seed ^= salt + 0x9e3779b9u + (seed << 6) + (seed >> 2);

        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;

        return seed;
    }
}