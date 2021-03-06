#include "makelevelset3.h"

using namespace std;

// find distance x0 is from segment x1-x2
static float point_segment_distance(const Vec3f& x0, const Vec3f& x1, const Vec3f& x2)
{
  Vec3f dx(x2 - x1);
  double m2 = mag2(dx);
  // find parameter value of closest point on segment
  float s12 = (float)(dot(x2 - x0, dx) / m2);
  if (s12 < 0)
  {
    s12 = 0;
  }
  else if (s12 > 1)
  {
    s12 = 1;
  }
  // and find the distance
  return dist(x0, s12 * x1 + (1 - s12) * x2);
}

// find distance x0 is from triangle x1-x2-x3
static float point_triangle_distance(const Vec3f& x0, const Vec3f& x1, const Vec3f& x2, const Vec3f& x3)
{
  // first find barycentric coordinates of closest point on infinite plane
  Vec3f x13(x1 - x3), x23(x2 - x3), x03(x0 - x3);
  float m13 = mag2(x13), m23 = mag2(x23), d = dot(x13, x23);
  float invdet = 1.f / max(m13 * m23 - d * d, 1e-30f);
  float a = dot(x13, x03), b = dot(x23, x03);
  // the barycentric coordinates themselves
  float w23 = invdet * (m23 * a - d * b);
  float w31 = invdet * (m13 * b - d * a);
  float w12 = 1 - w23 - w31;
  if (w23 >= 0 && w31 >= 0 && w12 >= 0)
  { // if we're inside the triangle
    return dist(x0, w23 * x1 + w31 * x2 + w12 * x3);
  }
  else
  {              // we have to clamp to one of the edges
    if (w23 > 0) // this rules out edge 2-3 for us
      return min(point_segment_distance(x0, x1, x2), point_segment_distance(x0, x1, x3));
    else if (w31 > 0) // this rules out edge 1-3
      return min(point_segment_distance(x0, x1, x2), point_segment_distance(x0, x2, x3));
    else // w12 must be >0, ruling out edge 1-2
      return min(point_segment_distance(x0, x1, x3), point_segment_distance(x0, x2, x3));
  }
}

static void check_neighbour(
    const vector<Vec3ui>& tri,
    const vector<Vec3f>& vtx,
    Array3f& phi,
    Array3i& closest_tri,
    const Vec3f& gx,
    int i0,
    int j0,
    int k0,
    int i1,
    int j1,
    int k1)
{
  // check if the triangle at (i1, j1, k1) is closer to gx than (i0, j0, k0)
  int tt = closest_tri(i1, j1, k1);
  if (tt >= 0)
  {
    unsigned int p, q, r;
    assign(tri[tt], p, q, r);
    float d = point_triangle_distance(gx, vtx[p], vtx[q], vtx[r]);
    if (d < phi(i0, j0, k0))
    {
      phi(i0, j0, k0) = d;
      closest_tri(i0, j0, k0) = tt;
    }
  }
}

static void sweep(
    const vector<Vec3ui>& tri,
    const vector<Vec3f>& vtx,
    Array3f& phi,
    Array3i& closest_tri,
    const Vec3f& minPos,
    float dx,
    float dy,
    float dz,
    int di,
    int dj,
    int dk)
{
  // calc start/end indices based on directions
  int i0, i1;
  if (di > 0)
  {
    i0 = 1;
    i1 = phi.ni;
  }
  else
  {
    i0 = phi.ni - 2;
    i1 = -1;
  }

  int j0, j1;
  if (dj > 0)
  {
    j0 = 1;
    j1 = phi.nj;
  }
  else
  {
    j0 = phi.nj - 2;
    j1 = -1;
  }

  int k0, k1;
  if (dk > 0)
  {
    k0 = 1;
    k1 = phi.nk;
  }
  else
  {
    k0 = phi.nk - 2;
    k1 = -1;
  }

  for (int k = k0; k != k1; k += dk)
  {
    for (int j = j0; j != j1; j += dj)
    {
      for (int i = i0; i != i1; i += di)
      {
        Vec3f gx(i * dx + minPos[0], j * dy + minPos[1], k * dz + minPos[2]);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i - di, j, k);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i, j - dj, k);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i - di, j - dj, k);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i, j, k - dk);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i - di, j, k - dk);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i, j - dj, k - dk);
        check_neighbour(tri, vtx, phi, closest_tri, gx, i, j, k, i - di, j - dj, k - dk);
      }
    }
  }
}

// calculate twice signed area of triangle (0,0)-(x1,y1)-(x2,y2)
// return an SOS-determined sign (-1, +1, or 0 only if it's a truly degenerate triangle)
static int orientation(double x1, double y1, double x2, double y2, double& twice_signed_area)
{
  twice_signed_area = y1 * x2 - x1 * y2;
  if (twice_signed_area > 0)
    return 1;
  else if (twice_signed_area < 0)
    return -1;
  else if (y2 > y1)
    return 1;
  else if (y2 < y1)
    return -1;
  else if (x1 > x2)
    return 1;
  else if (x1 < x2)
    return -1;
  else
    return 0; // only true when x1==x2 and y1==y2
}

// robust test of (x0,y0) in the triangle (x1,y1)-(x2,y2)-(x3,y3)
// if true is returned, the barycentric coordinates are set in a,b,c.
static bool point_in_triangle_2d(
    double x0,
    double y0,
    double x1,
    double y1,
    double x2,
    double y2,
    double x3,
    double y3,
    double& a,
    double& b,
    double& c)
{
  x1 -= x0;
  x2 -= x0;
  x3 -= x0;
  y1 -= y0;
  y2 -= y0;
  y3 -= y0;
  int signa = orientation(x2, y2, x3, y3, a);
  if (signa == 0)
    return false;
  int signb = orientation(x3, y3, x1, y1, b);
  if (signb != signa)
    return false;
  int signc = orientation(x1, y1, x2, y2, c);
  if (signc != signa)
    return false;
  double sum = a + b + c;
  assert(sum != 0); // if the SOS signs match and are nonkero, there's no way all of a, b, and c are zero.
  a /= sum;
  b /= sum;
  c /= sum;
  return true;
}

void make_level_set3(
    const vector<Vec3ui>& tri,
    const vector<Vec3f>& vtx,
    const Vec3f& minPos,
    const Vec3f& maxPos,
    const Vec3i& gridSize,
    Array3f& phi,
    const int exact_band)
{
  Vec3f span = maxPos - minPos;
  int ni = gridSize[0];
  int nj = gridSize[1];
  int nk = gridSize[2];

  float dx = span[0] / gridSize[0];
  float dy = span[1] / gridSize[1];
  float dz = span[2] / gridSize[2];

  // upper bound on distance
  phi.resize(ni, nj, nk, sqrtf(span[0] * span[0] + span[1] * span[1] + span[2] * span[2]));
  //phi.assign(sqrtf(span[0] * span[0] + span[1] * span[1] + span[2] * span[2]));

  Array3i closest_tri(ni, nj, nk, -1);

  // intersection_count(i,j,k) is # of tri intersections in (i-1,i]x{j}x{k}
  Array3i intersection_count(ni, nj, nk, 0);

  // we begin by initializing distances near the mesh, and figuring out intersection counts
  for (size_t t = 0; t < tri.size(); ++t)
  {
    // copy out the triangle indices
    unsigned int p, q, r;
    assign(tri[t], p, q, r);

    // "normalize" triangle coordinates, so grid cubes have size 1
    double fip = ((double)vtx[p][0] - minPos[0]) / dx;
    double fjp = ((double)vtx[p][1] - minPos[1]) / dy;
    double fkp = ((double)vtx[p][2] - minPos[2]) / dz;

    double fiq = ((double)vtx[q][0] - minPos[0]) / dx;
    double fjq = ((double)vtx[q][1] - minPos[1]) / dy;
    double fkq = ((double)vtx[q][2] - minPos[2]) / dz;

    double fir = ((double)vtx[r][0] - minPos[0]) / dx;
    double fjr = ((double)vtx[r][1] - minPos[1]) / dy;
    double fkr = ((double)vtx[r][2] - minPos[2]) / dz;

    // determine the neighbouring cubes for the triangle
    // why exact_band + 1? (because we start at origin?)
    int i0 = clamp(int(min(fip, fiq, fir)) - exact_band, 0, ni - 1);
    int i1 = clamp(int(max(fip, fiq, fir)) + exact_band + 1, 0, ni - 1);
    int j0 = clamp(int(min(fjp, fjq, fjr)) - exact_band, 0, nj - 1);
    int j1 = clamp(int(max(fjp, fjq, fjr)) + exact_band + 1, 0, nj - 1);
    int k0 = clamp(int(min(fkp, fkq, fkr)) - exact_band, 0, nk - 1);
    int k1 = clamp(int(max(fkp, fkq, fkr)) + exact_band + 1, 0, nk - 1);

    for (int k = k0; k <= k1; ++k)
    {
      for (int j = j0; j <= j1; ++j)
      {
        for (int i = i0; i <= i1; ++i)
        {
          // distance from grid corner to triangle
          Vec3f gx(i * dx + minPos[0], j * dy + minPos[1], k * dz + minPos[2]);
          float d = point_triangle_distance(gx, vtx[p], vtx[q], vtx[r]);
          if (d < phi(i, j, k))
          {
            phi(i, j, k) = d;
            closest_tri(i, j, k) = (int)t;
          }
        }
      }
    }

    // and do intersection counts
    j0 = clamp((int)ceil(min(fjp, fjq, fjr)), 0, nj - 1);
    j1 = clamp((int)floor(max(fjp, fjq, fjr)), 0, nj - 1);
    k0 = clamp((int)ceil(min(fkp, fkq, fkr)), 0, nk - 1);
    k1 = clamp((int)floor(max(fkp, fkq, fkr)), 0, nk - 1);
    for (int k = k0; k <= k1; ++k)
    {
      for (int j = j0; j <= j1; ++j)
      {
        double a, b, c;
        if (point_in_triangle_2d(j, k, fjp, fkp, fjq, fkq, fjr, fkr, a, b, c))
        {
          double fi = a * fip + b * fiq + c * fir; // intersection i coordinate
          int i_interval = int(ceil(fi));          // intersection is in (i_interval-1,i_interval]
          if (i_interval < 0)
          {
            // we enlarge the first interval to include everything to the -x direction
            ++intersection_count(0, j, k);
          }
          else if (i_interval < ni)
          {
            // we ignore intersections that are beyond the +x side of the grid
            ++intersection_count(i_interval, j, k);
          }
        }
      }
    }
  }

  // and now we fill in the rest of the distances with fast sweeping
  for (unsigned int pass = 0; pass < 2; ++pass)
  {
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, +1, +1, +1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, -1, -1, -1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, +1, +1, -1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, -1, -1, +1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, +1, -1, +1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, -1, +1, -1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, +1, -1, -1);
    sweep(tri, vtx, phi, closest_tri, minPos, dx, dy, dz, -1, +1, +1);
  }

  // then figure out signs (inside/outside) from intersection counts
  for (int k = 0; k < nk; ++k)
  {
    for (int j = 0; j < nj; ++j)
    {
      int total_count = 0;
      for (int i = 0; i < ni; ++i)
      {
        total_count += intersection_count(i, j, k);
        if (intersection_count(i, j, k) == 0)
        {
          // printf("0 at: %d, %d, %d\n", i, j, k);
        }
        if (total_count % 2 == 1)
        {
          // if parity of intersections so far is odd, we are inside the mesh
          phi(i, j, k) = -phi(i, j, k);
        }
      }
    }
  }
}

void make_level_set3_brute_force(
    const vector<Vec3ui>& tri,
    const vector<Vec3f>& vtx,
    const Vec3f& minPos,
    const Vec3f& maxPos,
    const Vec3i& gridSize,
    Array3f& phi)
{
  Vec3f span = maxPos - minPos;
  int ni = gridSize[0];
  int nj = gridSize[1];
  int nk = gridSize[2];

  float dx = span[0] / (gridSize[0] - 1);
  float dy = span[1] / (gridSize[1] - 1);
  float dz = span[2] / (gridSize[2] - 1);

  phi.resize(ni, nj, nk, FLT_MAX);
  // upper bound on distance
  //phi.assign(FLT_MAX);

  Array3i closest_tri(ni, nj, nk, -1);

  // intersection_count(i,j,k) is # of tri intersections in (i-1,i]x{j}x{k}
  Array3i intersection_count(ni, nj, nk, 0);

  // we begin by initializing distances near the mesh, and figuring out intersection counts
  for (size_t t = 0; t < tri.size(); ++t)
  {
    // copy out the triangle indices
    unsigned int p, q, r;
    assign(tri[t], p, q, r);

    for (int k = 0; k < nk; ++k)
    {
      for (int j = 0; j < nj; ++j)
      {
        for (int i = 0; i < ni; ++i)
        {
          // distance from grid corner to triangle
          Vec3f gx(i * dx + minPos[0], j * dy + minPos[1], k * dz + minPos[2]);
          float d = point_triangle_distance(gx, vtx[p], vtx[q], vtx[r]);
          if (d < phi(i, j, k))
          {
            phi(i, j, k) = d;
            closest_tri(i, j, k) = (int)t;
          }
        }
      }

      double fip = ((double)vtx[p][0] - minPos[0]) / dx;
      double fjp = ((double)vtx[p][1] - minPos[1]) / dy;
      double fkp = ((double)vtx[p][2] - minPos[2]) / dz;

      double fiq = ((double)vtx[q][0] - minPos[0]) / dx;
      double fjq = ((double)vtx[q][1] - minPos[1]) / dy;
      double fkq = ((double)vtx[q][2] - minPos[2]) / dz;

      double fir = ((double)vtx[r][0] - minPos[0]) / dx;
      double fjr = ((double)vtx[r][1] - minPos[1]) / dy;
      double fkr = ((double)vtx[r][2] - minPos[2]) / dz;

      // and do intersection counts
      for (int k = 0; k < nk; ++k)
      {
        for (int j = 0; j; ++j)
        {
          double a, b, c;
          if (point_in_triangle_2d(j, k, fjp, fkp, fjq, fkq, fjr, fkr, a, b, c))
          {
            double fi = a * fip + b * fiq + c * fir; // intersection i coordinate
            int i_interval = int(ceil(fi));          // intersection is in (i_interval-1,i_interval]
            if (i_interval < 0)
            {
              // we enlarge the first interval to include everything to the -x direction
              ++intersection_count(0, j, k);
            }
            else if (i_interval < ni)
            {
              // we ignore intersections that are beyond the +x side of the grid
              ++intersection_count(i_interval, j, k);
            }
          }
        }
      }
    }
  }

  // then figure out signs (inside/outside) from intersection counts
  for (int k = 0; k < nk; ++k)
  {
    for (int j = 0; j < nj; ++j)
    {
      int total_count = 0;
      for (int i = 0; i < ni; ++i)
      {
        total_count += intersection_count(i, j, k);
        if (intersection_count(i, j, k) == 0)
        {
          // printf("0 at: %d, %d, %d\n", i, j, k);
        }
        if (total_count % 2 == 1)
        {
          // if parity of intersections so far is odd, we are inside the mesh
          phi(i, j, k) = -phi(i, j, k);
        }
      }
    }
  }
}