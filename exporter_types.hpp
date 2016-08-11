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

inline float Length(const Vec3& v)
{
  return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

inline Vec3 operator-(const Vec3& a, const Vec3& b)
{
  return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator+(const Vec3& a, const Vec3& b)
{
  return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator/(const Vec3& a, float s)
{
  float r = 1.0f / s;
  return Vec3{ a.x * r, a.y * r, a.z * r };
}

inline Vec3 Normalize(const Vec3& v)
{
  float len = Length(v);
  if (len == 0)
    return Vec3{ 0,0,0 };

  float r = 1 / len;
  return Vec3{v.x * r, v.y * r, v.z * r};
}

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
