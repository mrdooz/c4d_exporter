#pragma once

inline void hash_combine(std::size_t& seed) { }

template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  hash_combine(seed, rest...);
}

template <>
struct hash<pair<int, int>>
{
  std::size_t operator()(const pair<int, int>& t) const
  {
    std::size_t ret = 0;
    hash_combine(ret, t.first, t.second);
    return ret;
  }
};


//------------------------------------------------------------------------------
struct vec2
{
  float x, y;
};

//------------------------------------------------------------------------------
struct vec3
{
  template <typename T>
  vec3(const T& v) : x(v.x), y(v.y), z(v.z)
  {
  }
  vec3(float x, float y, float z) : x(x), y(y), z(z) {}
  vec3() {}

  vec3& operator+=(const vec3& rhs)
  {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  vec3& operator/=(float s)
  {
    float r = 1.0f / s;
    x *= r;
    y *= r;
    z *= r;
    return *this;
  }

  float x, y, z;
};

inline float Length(const vec3& v)
{
  return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

inline float LengthSq(const vec3& v)
{
  return v.x*v.x + v.y*v.y + v.z*v.z;
}

inline vec3 operator-(const vec3& a, const vec3& b)
{
  return vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline vec3 operator+(const vec3& a, const vec3& b)
{
  return vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline vec3 operator/(const vec3& a, float s)
{
  float r = 1.0f / s;
  return vec3{ a.x * r, a.y * r, a.z * r };
}

inline vec3 operator*(float s, const vec3& v)
{
  return vec3{ s * v.x, s * v.y, s * v.z };
}

inline vec3 operator*(const vec3& v, float s)
{
  return vec3{ s * v.x, s * v.y, s * v.z };
}

inline vec3 Normalize(const vec3& v)
{
  float len = Length(v);
  if (len == 0)
    return vec3{ 0,0,0 };

  float r = 1 / len;
  return vec3{v.x * r, v.y * r, v.z * r};
}

inline float Dot(const vec3& a, const vec3& b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vec3 Cross(const vec3& a, const vec3& b)
{
  return vec3(
    (a.y * b.z) - (a.z * b.y),
    (a.z * b.x) - (a.x * b.z),
    (a.x * b.y) - (a.y * b.x));
}

inline vec3 Min(const vec3& lhs, const vec3& rhs)
{
  return vec3{min(lhs.x, rhs.x), min(lhs.y, rhs.y), min(lhs.z, rhs.z)};
}

inline vec3 Max(const vec3& lhs, const vec3& rhs)
{
  return vec3{ max(lhs.x, rhs.x), max(lhs.y, rhs.y), max(lhs.z, rhs.z) };
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
