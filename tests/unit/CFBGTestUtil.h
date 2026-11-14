/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#ifndef _CFBG_TEST_UTIL_H_
#define _CFBG_TEST_UTIL_H_

#include <cstdint>

namespace CFBGTest
{
    struct SplitMix64
    {
        uint64_t state;

        uint64_t Next()
        {
            uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        uint32_t Range(uint32_t lo, uint32_t hi)
        {
            return lo + static_cast<uint32_t>(Next() % (hi - lo + 1));
        }
    };

    inline uint32_t AbsDiff(uint64_t a, uint64_t b)
    {
        return static_cast<uint32_t>(a > b ? a - b : b - a);
    }
}

#endif // _CFBG_TEST_UTIL_H_
