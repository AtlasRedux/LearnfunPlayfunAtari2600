#ifndef _CC_LIB_RANDUTIL_H
#define _CC_LIB_RANDUTIL_H

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <utility>
#include <tuple>

// TODO: Template over RNG; don't include this here.
#include "arcfour.h"

// Creates another random stream, seeded by (and consuming some)
// of the input. Supplying a different n yields a different stream,
// which can be used to create fan-out in parallel.
//
// Caller owns new-ly allocated pointer.
inline ArcFour *Substream(ArcFour *rc, uint32_t n) {
  std::vector<uint8_t> buf;
  buf.resize(64);
  for (int i = 0; i < 4; i++) {
    buf[i] = n & 255;
    n >>= 8;
  }

  for (int i = 4; i < 64; i++) {
    buf[i] = rc->Byte();
  }

  ArcFour *nrc = new ArcFour(buf);
  nrc->Discard(256);
  return nrc;
}

// In [0, 1].
// Note that this approach samples uniformly from
// the interval, but loses precision. Consider using
// RandDouble and then converting to float if precision
// is important.
//
// PERF: There are other ways of doing this that are
// probably faster (this does invoke floating point
// division).
inline float RandFloat(ArcFour *rc) {
  uint32_t uu = 0U;
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  return (float)((uu   & 0x7FFFFFFF) /
                 (double)0x7FFFFFFF);
}

// PERF: As above.
inline double RandDouble(ArcFour *rc) {
  uint64_t uu = 0U;
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  // PERF: Maybe could be multiplying by the inverse?
  // It's a constant.
  return ((uu &   0x3FFFFFFFFFFFFFFFULL) /
          (double)0x3FFFFFFFFFFFFFFFULL);
}

// Sample in [0, 1).
inline double RandDoubleNot1(ArcFour *rc) {
  for (;;) {
    double d = RandDouble(rc);
    if (d < 1.0) return d;
  }
}

inline uint64_t Rand64(ArcFour *rc) {
  uint64_t uu = 0ULL;
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  return uu;
};

inline uint32_t Rand32(ArcFour *rc) {
  uint32_t uu = 0ULL;
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  return uu;
};

inline uint16_t Rand16(ArcFour *rc) {
  uint16_t uu = 0ULL;
  uu = rc->Byte() | (uu << 8);
  uu = rc->Byte() | (uu << 8);
  return uu;
};

// Generate uniformly distributed numbers in [0, n - 1].
// n must be greater than or equal to 2.
inline uint64_t RandTo(ArcFour *rc, uint64_t n) {
  // We use rejection sampling, as is standard, but with
  // a modulus that's the next largest power of two. This
  // means that we succeed half the time (worst case).
  //
  // First, compute the mask. Note that 2^k will be 100...00
  // and so 2^k-1 is 011...11. This is the mask we're looking
  // for. The input may not be a power of two, however. Make
  // sure any 1 bit is propagated to every position less
  // significant than it.
  // (PERF: I think this can now be done faster with std::countl_zero.
  // Benchmark it.)
  //
  // This ought to reduce to a constant if the argument is
  // a compile-time constant.
  uint64_t mask = n - 1;
  // TODO PERF: countl_zero
  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  mask |= mask >> 16;
  mask |= mask >> 32;

  // Now, repeatedly generate random numbers, modulo that
  // power of two.

  // Depending on how big n is, we may not need to generate 8 random
  // bytes! PERF: I only do one test here, but we could try to
  // distinguish all 8 if we wanted, or just use a loop. Benchmark.
  if (mask & ~0xFFFF) {
    for (;;) {
      const uint64_t x = Rand64(rc) & mask;
      if (x < n) return x;
    }
  } else {
    // 16-bit
    for (;;) {
      const uint64_t x = Rand16(rc) & mask;
      if (x < n) return x;
    }
  }
}

// As above, but for 32-bit ints.
inline uint32_t RandTo32(ArcFour *rc, uint32_t n) {
  uint32_t mask = n - 1;
  // TODO PERF: countl_zero
  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  mask |= mask >> 16;

  // Now, repeatedly generate random numbers, modulo that
  // power of two.

  // PERF: If the number is small, we only need Rand16, etc.
  for (;;) {
    const uint32_t x = Rand32(rc) & mask;
    if (x < n) return x;
  }
}

// TODO: A typical use of this is to take n elements from a set at
// random without replacement (shuffle and truncate). We can do this a
// bit faster by only randomizing a prefix of the vector (note how
// Shuffle only swaps in a triangular portion).

// Permute the elements of the vector uniformly at random.
template<class T>
static void Shuffle(ArcFour *rc, std::vector<T> *v) {
  if (v->size() <= 1) return;
  // PERF: Use Rand32 for small vectors.
  for (uint64_t i = v->size() - 1; i >= 1; i--) {
    uint64_t j = RandTo(rc, i + 1);
    if (i != j) {
      std::swap((*v)[i], (*v)[j]);
    }
  }
}

// Same, for an array.
template<class T, size_t N>
static void Shuffle(ArcFour *rc, std::array<T, N> *v) {
  if constexpr (N <= 1) return;
  // PERF: Use Rand32 for small arrays.
  for (uint64_t i = N - 1; i >= 1; i--) {
    uint64_t j = RandTo(rc, i + 1);
    if (i != j) {
      std::swap((*v)[i], (*v)[j]);
    }
  }
}

// Generates two at once, so needs some state.
struct RandomGaussian {
  double next = 0.0;
  ArcFour *rc = nullptr;
  bool have = false;
  explicit RandomGaussian(ArcFour *rc) : rc(rc) {}
  double Next() {
    if (have) {
      have = false;
      return next;
    } else {
      double v1, v2, sqnorm;
      // Generate a non-degenerate random point in the unit circle by
      // rejection sampling.
      do {
        v1 = 2.0 * RandDouble(rc) - 1.0;
        v2 = 2.0 * RandDouble(rc) - 1.0;
        sqnorm = v1 * v1 + v2 * v2;
      } while (sqnorm >= 1.0 || sqnorm == 0.0);
      double multiplier = sqrt(-2.0 * log(sqnorm) / sqnorm);
      next = v2 * multiplier;
      have = true;
      return v1 * multiplier;
    }
  }
};

// If you need many, RandomGaussian will be twice as fast.
inline double OneRandomGaussian(ArcFour *rc) {
  return RandomGaussian{rc}.Next();
}

// Adapted from numpy, based on Marsaglia & Tsang's method.
// Please see NUMPY.LICENSE.
struct RandomGamma {
  explicit RandomGamma(ArcFour *rc) : rc(rc), rg(rc) {}
  static constexpr double one_third = 1.0 / 3.0;

  double Exponential() {
    return -log(1.0 - RandDoubleNot1(rc));
  }

  double Next(double shape) {
    if (shape == 1.0) {
      return Exponential();
    } else if (shape < 1.0) {
      const double one_over_shape = 1.0 / shape;
      for (;;) {
        const double u = RandDoubleNot1(rc);
        const double v = Exponential();
        if (u < 1.0 - shape) {
          const double x = std::pow(u, one_over_shape);
          if (x <= v) {
            return x;
          }
        } else {
          const double y = -log((1.0 - u) / shape);
          const double x = std::pow(1.0 - shape + shape * y,
                                    one_over_shape);
          if (x <= v + y) {
            return x;
          }
        }
      }
    } else {
      const double b = shape - one_third;
      const double c = 1.0 / sqrt(9.0 * b);
      for (;;) {
        double x, v;
        do {
          x = rg.Next();
          v = 1.0 + c * x;
        } while (v <= 0.0);

        const double v_cubed = v * v * v;
        const double x_squared = x * x;
        const double u = RandDoubleNot1(rc);
        if (u < 1.0 - 0.0331 * x_squared * x_squared ||
            log(u) < 0.5 * x_squared + b * (1.0 - v_cubed + log(v_cubed))) {
          return b * v_cubed;
        }
      }
    }
  }

  ArcFour *rc = nullptr;
  RandomGaussian rg;
};

inline double OneRandomGamma(ArcFour *rc, double shape) {
  return RandomGamma(rc).Next(shape);
}

// Reminder: Beta(a, b) gives the probability distribution
// when we have 'a' successful trials and 'b' unsuccessful
// trials. (The expected value is a/(a + b)).
inline double RandomBeta(ArcFour *rc, double a, double b) {
  if (a <= 1.0 && b <= 1.0) {
    for (;;) {
      const double u = RandDoubleNot1(rc);
      const double v = RandDoubleNot1(rc);
      const double x = std::pow(u, 1.0 / a);
      const double y = std::pow(v, 1.0 / b);
      const double x_plus_y = x + y;
      if (x_plus_y <= 1.0) {
        if (x_plus_y > 0.0) {
          return x / x_plus_y;
        } else {
          double log_x = log(u) / a;
          double log_y = log(v) / b;
          const double log_m = log_x > log_y ? log_x : log_y;
          log_x -= log_m;
          log_y -= log_m;

          return exp(log_x - log(exp(log_x) + exp(log_y)));
        }
      }
    }
  } else {
    RandomGamma rg(rc);
    const double ga = rg.Next(a);
    const double gb = rg.Next(b);
    return ga / (ga + gb);
  }
}

// Get a uniformly random 3D unit vector, i.e., a point on a sphere.
inline std::tuple<double, double, double> RandomUnit3D(ArcFour *rc) {
  for (;;) {
    double a = 2.0 * RandDouble(rc) - 1.0;
    double b = 2.0 * RandDouble(rc) - 1.0;
    double c = 2.0 * RandDouble(rc) - 1.0;

    double sq_dist = a * a + b * b + c * c;
    if (sq_dist <= 1.0 && sq_dist > 0.0) {
      double norm = 1.0 / std::sqrt(sq_dist);
      return std::make_tuple(norm * a, norm * b, norm * c);
    }
  }
}

// Get a random 4D unit vector.
inline std::tuple<double, double, double, double> RandomUnit4D(
    ArcFour *rc) {
  // TODO PERF: This can be done more efficiently using
  // the Gaussian distribution, although it is also a fancier
  // algorithm. For 4D, the rejection approach is reasonable
  // (expected number of calls to RandomDouble is about 13).

  // Conceptually we are generating a random point in a
  // 2x2x2x2 hypercube and then checking whether it is in
  // the unit 4d hypersphere. If so, we normalize it and we
  // are done.
  for (;;) {
    double a = 2.0 * RandDouble(rc) - 1.0;
    double b = 2.0 * RandDouble(rc) - 1.0;
    double c = 2.0 * RandDouble(rc) - 1.0;
    double d = 2.0 * RandDouble(rc) - 1.0;

    double sq_dist = a * a + b * b + c * c + d * d;
    if (sq_dist <= 1.0 && sq_dist > 0.0) {
      double norm = 1.0 / std::sqrt(sq_dist);
      return std::make_tuple(norm * a, norm * b, norm * c, norm * d);
    }
  }
}

#endif
