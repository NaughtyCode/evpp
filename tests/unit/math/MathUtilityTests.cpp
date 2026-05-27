#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "MathUtility.h"
#include "MemoryUtility.h"
#include "PlatformSIMD.h"
#include "Vector3D.h"

TEST_CASE("Vector3f supports basic vector operations") {
  CullingEngine::Vector3f zero;
  CHECK(zero.X == doctest::Approx(0.0f));
  CHECK(zero.Y == doctest::Approx(0.0f));
  CHECK(zero.Z == doctest::Approx(0.0f));
  CHECK(zero.Normalize().Length() == doctest::Approx(0.0f));

  const CullingEngine::Vector3f xAxis(1.0f, 0.0f, 0.0f);
  const CullingEngine::Vector3f yAxis(0.0f, 1.0f, 0.0f);
  const CullingEngine::Vector3f zAxis = xAxis.Cross(yAxis);

  CHECK(zAxis.X == doctest::Approx(0.0f));
  CHECK(zAxis.Y == doctest::Approx(0.0f));
  CHECK(zAxis.Z == doctest::Approx(1.0f));
  CHECK(xAxis.Dot(yAxis) == doctest::Approx(0.0f));
  CHECK(CullingEngine::Vector3f(3.0f, 4.0f, 0.0f).Length() ==
        doctest::Approx(5.0f));

  const CullingEngine::Vector3f normalized =
      CullingEngine::Vector3f(0.0f, 3.0f, 4.0f).Normalize();
  CHECK(normalized.X == doctest::Approx(0.0f));
  CHECK(normalized.Y == doctest::Approx(0.6f));
  CHECK(normalized.Z == doctest::Approx(0.8f));

  const CullingEngine::Vector3f difference =
      CullingEngine::Vector3f(4.0f, 5.0f, 6.0f) -
      CullingEngine::Vector3f(1.0f, 2.0f, 3.0f);
  CHECK(difference.X == doctest::Approx(3.0f));
  CHECK(difference.Y == doctest::Approx(3.0f));
  CHECK(difference.Z == doctest::Approx(3.0f));
}

TEST_CASE("FloatArray compares matrices and distances") {
  alignas(8) std::array<float, 16> a{1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,
                                     7.0f,  8.0f,  9.0f,  10.0f, 11.0f, 12.0f,
                                     13.0f, 14.0f, 15.0f, 16.0f};
  alignas(8) std::array<float, 16> b = a;

  CHECK(CullingEngine::FloatArray::ContainSameData16(a.data(), b.data()));
  b[7] = 99.0f;
  CHECK_FALSE(CullingEngine::FloatArray::ContainSameData16(a.data(), b.data()));

  std::array<float, 17> unalignedA{};
  std::array<float, 17> unalignedB{};
  for (std::size_t i = 0; i < 16; ++i) {
    unalignedA[i + 1] = static_cast<float>(i);
    unalignedB[i + 1] = static_cast<float>(i);
  }
  CHECK(CullingEngine::FloatArray::ContainSameData16(unalignedA.data() + 1,
                                                     unalignedB.data() + 1));

  const float p0[3] = {1.0f, 2.0f, 3.0f};
  const float p1[3] = {4.0f, 6.0f, 3.0f};
  CHECK(CullingEngine::FloatArray::CalculateSquareDistance3(p0, p1) ==
        doctest::Approx(25.0f));
}

TEST_CASE("Matrix4x4 transposes input and multiplies matrices") {
  const float source[16] = {1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,
                            7.0f,  8.0f,  9.0f,  10.0f, 11.0f, 12.0f,
                            13.0f, 14.0f, 15.0f, 16.0f};

  CullingEngine::Matrix4x4 matrix;
  matrix.UpdateTranspose(source);

  alignas(16) float row[4] = {};
  _mm_storeu_ps(row, matrix.Row[0]);
  CHECK(row[0] == doctest::Approx(1.0f));
  CHECK(row[1] == doctest::Approx(5.0f));
  CHECK(row[2] == doctest::Approx(9.0f));
  CHECK(row[3] == doctest::Approx(13.0f));

  const float identitySource[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                    0.0f, 0.0f, 0.0f, 1.0f};

  CullingEngine::Matrix4x4 identity;
  identity.UpdateTranspose(identitySource);

  CullingEngine::Matrix4x4 product;
  CullingEngine::Matrix4x4::Multiply(identity, matrix, product);

  alignas(16) float productRow[4] = {};
  _mm_storeu_ps(productRow, product.Row[2]);
  CHECK(productRow[0] == doctest::Approx(3.0f));
  CHECK(productRow[1] == doctest::Approx(7.0f));
  CHECK(productRow[2] == doctest::Approx(11.0f));
  CHECK(productRow[3] == doctest::Approx(15.0f));
}

TEST_CASE("byte swap utility reverses bytes") {
  CHECK(CullingEngine::CullingEngine_bswap_64(0x0102030405060708ull) ==
        0x0807060504030201ull);
}
