/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/math/math.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MATH_HPP
#define MATH_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../types.hpp"

struct valve_random {
  static constexpr int table_size = 32;
  static constexpr int ia = 16807;
  static constexpr int im = 2147483647;
  static constexpr int iq = 127773;
  static constexpr int ir = 2836;
  static constexpr int ndiv = 1 + ((im - 1) / table_size);
  static constexpr double am = 1.0 / static_cast<double>(im);
  static constexpr double rnmx = 1.0 - 1.2e-7;

  int seed_value = 0;
  int shuffle_value = 0;
  int table[table_size]{};

  void set_seed(int seed) {
    seed_value = seed < 0 ? seed : -seed;
    shuffle_value = 0;
  }

  int generate_random_number() {
    if (seed_value <= 0 || shuffle_value == 0) {
      seed_value = -seed_value < 1 ? 1 : -seed_value;
      for (int j = table_size + 7; j >= 0; --j) {
        const int k = seed_value / iq;
        seed_value = ia * (seed_value - (k * iq)) - (ir * k);
        if (seed_value < 0) {
          seed_value += im;
        }
        if (j < table_size) {
          table[j] = seed_value;
        }
      }
      shuffle_value = table[0];
    }

    const int k = seed_value / iq;
    seed_value = ia * (seed_value - (k * iq)) - (ir * k);
    if (seed_value < 0) {
      seed_value += im;
    }

    int j = shuffle_value / ndiv;
    if (j >= table_size || j < 0) {
      j &= table_size - 1;
    }
    shuffle_value = table[j];
    table[j] = seed_value;
    return shuffle_value;
  }

  float random_float(float lo, float hi) {
    double value = am * static_cast<double>(generate_random_number());
    if (value > rnmx) {
      value = rnmx;
    }
    return static_cast<float>((value * static_cast<double>(hi - lo)) + static_cast<double>(lo));
  }

  int random_int(int lo, int hi) {
    if (hi <= lo) {
      return lo;
    }
    const std::uint32_t range = static_cast<std::uint32_t>(hi - lo) + 1U;
    const std::uint32_t limit = 0x80000000U - (0x80000000U % range);
    std::uint32_t value = 0;
    do {
      value = static_cast<std::uint32_t>(generate_random_number());
    } while (value >= limit);
    return lo + static_cast<int>(value % range);
  }
};

inline constexpr float radpi = 57.295779513082f;

inline constexpr float pideg = 0.017453293f;

inline static float distance_3d(Vec3 location_one, Vec3 location_two) {
  return sqrt(((location_one.y - location_two.y)*(location_one.y - location_two.y)) +
	      ((location_one.x - location_two.x)*(location_one.x - location_two.x)) +
	      ((location_one.z - location_two.z)*(location_one.z - location_two.z)));
}

inline static float distance_squared_2d(Vec3 location_one, Vec3 location_two) {
  return (location_one.x - location_two.x)*(location_one.x - location_two.x) + (location_one.y - location_two.y)*(location_one.y - location_two.y);
}

inline static float azimuth_to_signed(float yaw) {
  yaw = std::fmod(yaw, 360.0f);
  if (yaw > 180.0f) yaw -= 360.0f;
  if (yaw <= -180.0f) yaw += 360.0f;
  return yaw;
}

inline static void angle_vectors(Vec3 angles, Vec3* forward, Vec3* right, Vec3* up) {
  double sp, sy, sr, cp, cy, cr;
  sincos(angles.x * pideg, &sp, &cp);
  sincos(angles.y * pideg, &sy, &cy);

  if (forward) {
    forward->x = cp * cy;
    forward->y = cp * sy;
    forward->z = -sp;
  }

  if (right || up) {
    sincos(angles.z * pideg, &sr, &cr);

    if (right) {
      right->x = (-1 * sr * sp * cy + -1 * cr * -sy);
      right->y = (-1 * sr * sp * sy + -1 * cr * cy);
      right->z = -1 * sr * cp;
    }

    if (up) {
      up->x = (cr * sp * cy + -sr * -sy);
      up->y = (cr * sp * sy + -sr * cy);
      up->z = cr * cp;
    }
  }
}

inline float clampf(float v, float lo, float hi) { return std::clamp(v, lo, hi); }

#endif
