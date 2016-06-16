#pragma once

//------------------------------------------------------------------------------
struct Vec2
{
  float x, y;
};

//------------------------------------------------------------------------------
struct Vec3
{
  template <typename T>
  Vec3(const T& v) : x(v.x), y(v.y), z(v.z)
  {
  }
  Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
  Vec3() {}
  float x, y, z;
};

//------------------------------------------------------------------------------
struct Vec4
{
  float x, y, z, w;
};

//------------------------------------------------------------------------------
struct Color
{
  Color(float r, float g, float b) : r(r), g(g), b(b) {}
  Color() : r(0), g(0), b(0) {}
  float r, g, b;
};
