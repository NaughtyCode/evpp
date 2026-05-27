#pragma once

#include <sys/stat.h>
#include <sys/types.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "PlatformSIMD.h"

namespace CullingEngine {
struct Vector3f {
  float X, Y, Z;

  Vector3f() : X(0.0), Y(0.0), Z(0.0) {}
  Vector3f(float x, float y, float z) : X(x), Y(y), Z(z) {}
  Vector3f(const float* p) : X(p[0]), Y(p[1]), Z(p[2]) {}

  Vector3f Normalize() const {
    float len = Length();
    if (len == 0.0f) {
      return Vector3f();
    }
    float invLen = 1.0f / len;
    return Vector3f(X * invLen, Y * invLen, Z * invLen);
  }

  Vector3f Cross(const Vector3f& rhs) const {
    return Vector3f(Y * rhs.Z - Z * rhs.Y, Z * rhs.X - X * rhs.Z,
                    X * rhs.Y - Y * rhs.X);
  }

  float Dot(const Vector3f& rhs) const {
    return X * rhs.X + Y * rhs.Y + Z * rhs.Z;
  }

  float Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }

  Vector3f operator-(const Vector3f& rhs) const {
    return Vector3f(this->X - rhs.X, this->Y - rhs.Y, this->Z - rhs.Z);
  }
};
}  // namespace CullingEngine
