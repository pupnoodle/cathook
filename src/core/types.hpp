/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/types.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef VEC_HPP
#define VEC_HPP

#include <algorithm>
#include <chrono>
#include <cmath>

struct Vec2 {
  int x = 0, y = 0;
};

struct Vec3 {
  float x = 0.0, y = 0.0, z = 0.0;

  Vec3 operator+(const Vec3 v) const {
    return Vec3{x + v.x, y + v.y, z + v.z};
  }

  Vec3 operator*(const Vec3 v) const {
    return Vec3{x * v.x, y * v.y, z * v.z};
  }

  Vec3 operator*(float v) const {
    return Vec3{x * v, y * v, z * v};
  }

  Vec3 operator*(int v) const {
    return Vec3{x * v, y * v, z * v};
  }

  Vec3& operator-=(const Vec3 v) {
    x -= v.x; y -= v.y; z -= v.z; return *this;
  }

  Vec3& operator+=(const Vec3 v) {
    x += v.x; y += v.y; z += v.z; return *this;
  }

  Vec3& operator+=(float v) {
    x += v; y += v; z += v; return *this;
  }

  Vec3 operator-(const Vec3 v) const {
    return Vec3{x - v.x, y - v.y, z - v.z};
  }

  bool operator==(const Vec3 v) const {
    return (this->x == v.x && this->y == v.y && this->z == v.z);
  }

  bool operator!=(const Vec3 v) const {
    return !(*this == v);
  }
};

struct __attribute__((aligned(16))) Vec3_aligned {
  float x = 0.0, y = 0.0, z = 0.0, w = 0.0;
};

struct RGBA {
  int r = 255, g = 255, b = 255, a = 255;
};

struct RGBA_float {
  float r = 1.0, g = 1.0, b = 1.0, a = 1.0;
  bool rainbow = false;

  RGBA_float resolved() const {
    if (!rainbow) return *this;

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const float seconds = std::chrono::duration<float>(now).count();
    const float hue = seconds * 0.15f - std::floor(seconds * 0.15f);
    RGBA_float result = *this;
    const float h = hue * 6.0f;
    const float c = 0.95f;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    result.rainbow = false;
    if (h < 1.0f) { result.r = c; result.g = x; result.b = 0.0f; }
    else if (h < 2.0f) { result.r = x; result.g = c; result.b = 0.0f; }
    else if (h < 3.0f) { result.r = 0.0f; result.g = c; result.b = x; }
    else if (h < 4.0f) { result.r = 0.0f; result.g = x; result.b = c; }
    else if (h < 5.0f) { result.r = x; result.g = 0.0f; result.b = c; }
    else { result.r = c; result.g = 0.0f; result.b = x; }
    return result;
  }

  RGBA to_RGBA() const {
    const auto color = resolved();
    return RGBA{
      .r = std::clamp(static_cast<int>(color.r * 255.0f), 0, 255),
      .g = std::clamp(static_cast<int>(color.g * 255.0f), 0, 255),
      .b = std::clamp(static_cast<int>(color.b * 255.0f), 0, 255),
      .a = std::clamp(static_cast<int>(color.a * 255.0f), 0, 255)
    };
  }

};

typedef float VMatrix[4][4];

struct view_setup {

  int x;
  int m_nUnscaledX;

  int y;
  int m_nUnscaledY;

  int width;
  int m_nUnscaledWidth;

  int height;

  int m_eStereoEye;
  int m_nUnscaledHeight;

  bool		m_bOrtho;

  float		m_OrthoLeft;
  float		m_OrthoTop;
  float		m_OrthoRight;
  float		m_OrthoBottom;

  float		fov;

  float		fovViewmodel;

  Vec3		origin;

  Vec3		angles;

  float		zNear;

  float		zFar;

  float		zNearViewmodel;

  float		zFarViewmodel;

  bool		m_bRenderToSubrectOfLargerScreen;

  float		m_flAspectRatio;

  bool		m_bOffCenter;
  float		m_flOffCenterTop;
  float		m_flOffCenterBottom;
  float		m_flOffCenterLeft;
  float		m_flOffCenterRight;

  bool		m_bDoBloomAndToneMapping;

  bool		m_bCacheFullSceneState;

  bool        m_bViewToProjectionOverride;
  VMatrix     m_ViewToProjection;
};

#endif
