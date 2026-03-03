#pragma once
#include <cstdint>
#include <limits>

inline uint64_t max_from_bits(unsigned N)
{
  return (N == 64) ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << N) - 1);
}
