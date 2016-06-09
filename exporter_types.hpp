#pragma once

//------------------------------------------------------------------------------
struct Vec2
{
  float x, y;
};

//------------------------------------------------------------------------------
template <typename T>
struct Vec3
{
  template <typename U>
  Vec3(const U& v) : x(v.x), y(v.y), z(v.z)
  {
  }
  Vec3(T x, T y, T z) : x(x), y(y), z(z) {}
  Vec3() {}
  T x, y, z;
};

typedef Vec3<float> Vec3f;

//------------------------------------------------------------------------------
struct Color
{
  Color(float r, float g, float b) : r(r), g(g), b(b) {}
  Color() : r(0), g(0), b(0) {}
  float r, g, b;
};
