#include "json_exporter.hpp"
#include <dlib/json_writer.hpp>
#include "exporter_utils.hpp"
#include "sdf_gen.hpp"
#include "bit_utils.hpp"

#if WITH_EMBREE
#include <c:/projects/embree/common/math/affinespace.h>
#endif
#include "contrib/sdf/makelevelset3.h"

extern vector<char> buffer;

vec3 ClosestPtvec3Triangle(
    const vec3& p, const vec3& a, const vec3& b, const vec3& c, TriangleFeature* feature)
{

  // Check if P in vertex region outside A
  vec3 ab = b - a;
  vec3 ac = c - a;
  vec3 ap = p - a;
  float d1 = Dot(ab, ap);
  float d2 = Dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f)
  {
    *feature = FeatureVertexA;
    return a; // barycentric coordinates (1,0,0)
  }

  // Check if P in vertex region outside B
  vec3 bp = p - b;
  float d3 = Dot(ab, bp);
  float d4 = Dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3)
  {
    *feature = FeatureVertexB;
    return b; // barycentric coordinates (0,1,0)
  }

  // Check if P in edge region of AB, if so return projection of P onto AB
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
  {
    *feature = FeatureEdgeAB;
    float v = d1 / (d1 - d3);
    return a + v * ab; // barycentric coordinates (1-v,v,0)
  }

  // Check if P in vertex region outside C
  vec3 cp = p - c;
  float d5 = Dot(ab, cp);
  float d6 = Dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6)
  {
    *feature = FeatureVertexC;
    return c; // barycentric coordinates (0,0,1)
  }

  // Check if P in edge region of AC, if so return projection of P onto AC
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
  {
    *feature = FeatureEdgeAC;
    float w = d2 / (d2 - d6);
    return a + w * ac; // barycentric coordinates (1-w,0,w)
  }

  // Check if P in edge region of BC, if so return projection of P onto BC
  float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
  {
    *feature = FeatureEdgeBC;
    float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + w * (c - b); // barycentric coordinates (0,1-w,w)
  }

  *feature = FeatureFace;
  // P inside face region. Compute Q through its barycentric coordinates (u,v,w)
  float denom = 1.0f / (va + vb + vc);
  float v = vb * denom;
  float w = vc * denom;
  return a + ab * v + ac * w; // = u*a + v*b + w*c, u = va * denom = 1.0f - v - w
}


#if WITH_EMBREE
//------------------------------------------------------------------------------
static bool TrianglesFromMesh(const ImMesh* mesh, vector<ImMeshFace>* triangles, vector<ImMeshVertex>* vertices)
{
  // find index and pos data streams
  const ImMesh::DataStream* indexStream = mesh->StreamByType(ImMesh::DataStream::Type::Index16);
  if (!indexStream)
    indexStream = mesh->StreamByType(ImMesh::DataStream::Type::Index32);

  const ImMesh::DataStream* posStream = mesh->StreamByType(ImMesh::DataStream::Type::Pos);

  if (!indexStream || !posStream)
  {
    LOG(1, "Unable to find index or pos stream for mesh: %s", mesh->name.c_str());
    return false;
  }

  size_t numIndices = indexStream->NumElems();
  size_t numVertices = posStream->NumElems();

  triangles->resize(numIndices / 3);
  vertices->resize(numVertices);

  if (indexStream->type == ImMesh::DataStream::Type::Index16)
  {
    int16_t* ptr = (int16_t*)(indexStream->data.data());
    for (size_t i = 0; i < numIndices / 3; ++i)
    {
      (*triangles)[i].v0 = *ptr++;
      (*triangles)[i].v1 = *ptr++;
      (*triangles)[i].v2 = *ptr++;
    }
  }
  else
  {
    int32_t* ptr = (int32_t*)(indexStream->data.data());
    for (size_t i = 0; i < numIndices / 3; ++i)
    {
      (*triangles)[i].v0 = *ptr++;
      (*triangles)[i].v1 = *ptr++;
      (*triangles)[i].v2 = *ptr++;
    }
  }

  vec3* vtx = (vec3*)posStream->data.data();
  for (size_t i = 0; i < numVertices; ++i)
  {
    (*vertices)[i] = ImMeshVertex{ vtx->x, vtx->y, vtx->z, 0.f };
    vtx++;
  }

  return true;
}

//------------------------------------------------------------------------------
static void CreateSDF(const ImScene& scene, const Options& options, JsonWriter* w)
{
  using melange::Vector;

  // find the scene box
  const ImPrimitiveCube* bbScene = nullptr;
  for (ImPrimitive* p : scene.primitives)
  {
    if (p->name == "scene" && p->type == ImPrimitive::Type::Cube)
    {
      bbScene = static_cast<ImPrimitiveCube*>(p);
      break;
    }
  }

  if (!bbScene)
  {
    LOG(1, "Unable to find scene box");
    return;
  }

  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

  RTCDevice rtcDevice = rtcNewDevice(NULL);
  RTCScene rtcScene = rtcDeviceNewScene(rtcDevice, RTC_SCENE_STATIC, RTC_INTERSECT1);

  vector<u32> geomIds;

  vector<vec3> triangleCenters;

  // Add all the meshes to the embree scene
  for (const ImMesh* mesh : scene.meshes)
  {
    vector<ImMeshFace> triangles;
    vector<ImMeshVertex> vertices;

    if (!TrianglesFromMesh(mesh, &triangles, &vertices))
      continue;

    triangleCenters.reserve(triangleCenters.size() + triangles.size());

    u32 geomId = rtcNewTriangleMesh(rtcScene, RTC_GEOMETRY_STATIC, triangles.size(), vertices.size(), 1);
    geomIds.push_back(geomId);

    // offset is stored in slot 0..
    Vector ofs = mesh->xformGlobal.mtx[0];
    Vector v0 = mesh->xformGlobal.mtx[1];
    Vector v1 = mesh->xformGlobal.mtx[2];
    Vector v2 = mesh->xformGlobal.mtx[3];

    embree::AffineSpace3f xfm;
    xfm.l.vx = { (float)v0.x, (float)v0.y, (float)v0.z };
    xfm.l.vy = { (float)v1.x, (float)v1.y, (float)v1.z };
    xfm.l.vz = { (float)v2.x, (float)v2.y, (float)v2.z };
    xfm.p = { (float)ofs.x, (float)ofs.y, (float)ofs.z };
    rtcSetTransform(rtcScene, geomId, RTC_MATRIX_COLUMN_MAJOR, (float*)&xfm);

    // store all the triangle centers for the ray tracing
    for (size_t i = 0; i < triangles.size(); ++i)
    {
      const ImMeshVertex& v0 = vertices[triangles[i].a];
      const ImMeshVertex& v1 = vertices[triangles[i].b];
      const ImMeshVertex& v2 = vertices[triangles[i].c];

      Vector center =
        (Vector(v0.x, v0.y, v0.z) + Vector(v1.x, v1.y, v1.z) + Vector(v2.x, v2.y, v2.z)) / 3;

      center = mesh->xformGlobal.mtx * center;
      triangleCenters.push_back(vec3{ (float)center.x, (float)center.y, (float)center.z });
    }

    ImMeshVertex* rtcVertices = (ImMeshVertex*)rtcMapBuffer(rtcScene, geomId, RTC_VERTEX_BUFFER);
    memcpy(rtcVertices, vertices.data(), vertices.size() * sizeof(ImMeshVertex));
    rtcUnmapBuffer(rtcScene, geomId, RTC_VERTEX_BUFFER);

    ImMeshFace* rtcTriangles = (ImMeshFace*)rtcMapBuffer(rtcScene, geomId, RTC_INDEX_BUFFER);
    memcpy(rtcTriangles, triangles.data(), triangles.size() * sizeof(ImMeshFace));
    rtcUnmapBuffer(rtcScene, geomId, RTC_INDEX_BUFFER);
  }

  rtcCommit(rtcScene);

  // Traverse the scene grid, and find the closest point

  int gridSize = options.gridSize;

  vec3 size = bbScene->size;
  vec3 inc = { size.x / gridSize, size.y / gridSize, size.z / gridSize };
  vec3 orgPos = { -size.x / 2 + inc.x / 2, -size.y / 2 + inc.y / 2, -size.z / 2 + inc.z / 2 };
  vec3 curPos = orgPos;

  vector<float> sdf(gridSize * gridSize * gridSize);
  size_t idx = 0;

  float lastPct = 0;
  printf("==] SDF [==\n=");
  for (size_t i = 0; i < gridSize; ++i)
  {
    float curPct = (float)i / gridSize;
    if (curPct - lastPct > 0.1)
    {
      printf(".");
      lastPct = curPct;
    }

    curPos.y = orgPos.y;
    for (size_t j = 0; j < gridSize; ++j)
    {
      curPos.x = orgPos.x;
      for (size_t k = 0; k < gridSize; ++k)
      {
        // find the closest distance from the current point to each triangle center
        float closest = FLT_MAX;
        for (size_t n = 0; n < triangleCenters.size(); ++n)
        {
          RTCRay ray;
          ray.org[0] = curPos.x;
          ray.org[1] = curPos.y;
          ray.org[2] = curPos.z;

          vec3 dir = Normalize(triangleCenters[n] - curPos);
          ray.dir[0] = dir.x;
          ray.dir[1] = dir.y;
          ray.dir[2] = dir.z;

          ray.tnear = 0;
          ray.tfar = closest;
          ray.time = 0;

          ray.geomID = RTC_INVALID_GEOMETRY_ID;
          ray.primID = RTC_INVALID_GEOMETRY_ID;
          ray.mask = -1;

          rtcIntersect(rtcScene, ray);

          if (ray.geomID != RTC_INVALID_GEOMETRY_ID && ray.tfar < closest)
          {
            closest = ray.tfar;
          }
        }

        sdf[idx++] = closest;

        curPos.x += inc.x;
      }

      curPos.y += inc.y;
    }

    curPos.z += inc.z;

  }

  printf("\n");
  for (u32 geomId : geomIds)
  {
    rtcDeleteGeometry(rtcScene, geomId);
  }

  rtcDeleteScene(rtcScene);
  rtcDeleteDevice(rtcDevice);

  size_t oldSize = buffer.size();
  size_t dataSize = sdf.size() * sizeof(float);
  buffer.resize(oldSize + dataSize);
  memcpy(buffer.data() + oldSize, sdf.data(), dataSize);

  JsonWriter::JsonScope s(w, "sdf", JsonWriter::CompoundType::Object);
  w->Emit("dataOffset", oldSize);
  w->Emit("dataSize", dataSize);
  w->Emit("gridSize", gridSize);
}
#endif

//------------------------------------------------------------------------------
static bool TrianglesFromMesh2(
    const ExportInstance& instance,
    const ImMesh* mesh,
    vector<Vec3ui>* triangles,
    vector<Vec3f>* vertices,
    vec3* minPos,
    vec3* maxPos)
{
  // find index and pos data streams
  const ImMesh::DataStream* indexStream = mesh->StreamByType(ImMesh::DataStream::Type::Index16);
  if (!indexStream)
    indexStream = mesh->StreamByType(ImMesh::DataStream::Type::Index32);

  const ImMesh::DataStream* posStream = mesh->StreamByType(ImMesh::DataStream::Type::Pos);

  if (!indexStream || !posStream)
  {
    instance.Log(1, "Unable to find index or pos stream for mesh: %s", mesh->name.c_str());
    return false;
  }

  size_t numIndices = indexStream->NumElems();
  size_t numVertices = posStream->NumElems();

  size_t oldTriIdx = triangles->size();
  size_t oldVtxIdx = vertices->size();

  triangles->resize(oldTriIdx + numIndices / 3);
  vertices->resize(oldVtxIdx + numVertices);

  if (indexStream->type == ImMesh::DataStream::Type::Index16)
  {
    int16_t* ptr = (int16_t*)(indexStream->data.data());
    for (size_t i = 0; i < numIndices / 3; ++i)
    {
      (*triangles)[i + oldTriIdx].v[0] = *ptr++;
      (*triangles)[i + oldTriIdx].v[1] = *ptr++;
      (*triangles)[i + oldTriIdx].v[2] = *ptr++;
    }
  }
  else
  {
    int32_t* ptr = (int32_t*)(indexStream->data.data());
    for (size_t i = 0; i < numIndices / 3; ++i)
    {
      (*triangles)[i + oldTriIdx].v[0] = *ptr++;
      (*triangles)[i + oldTriIdx].v[1] = *ptr++;
      (*triangles)[i + oldTriIdx].v[2] = *ptr++;
    }
  }

  vec3* vtx = (vec3*)posStream->data.data();
  for (size_t i = 0; i < numVertices; ++i)
  {
    melange::Vector v = melange::Vector{ vtx->x, vtx->y, vtx->z };
    v = mesh->xformGlobal.mtx * v;
    vec3 vv = { (float)v.x, (float)v.y, (float)v.z };
    (*vertices)[i + oldVtxIdx] = Vec3f{ vv.x, vv.y, vv.z };
    *minPos = vec3{ min(vv.x, minPos->x), min(vv.y, minPos->y), min(vv.z, minPos->z) };
    *maxPos = vec3{ max(vv.x, maxPos->x), max(vv.y, maxPos->y), max(vv.z, maxPos->z) };
    vtx++;
  }

  return true;
}

//------------------------------------------------------------------------------
static void CreateSDF2(const ExportInstance& instance, JsonWriter* w)
{
  using melange::Vector;

#if 0
  // find the scene box
  const ImPrimitiveCube* bbScene = nullptr;
  for (ImPrimitive* p : scene.primitives)
  {
    if (p->name == "scene" && p->type == ImPrimitive::Type::Cube)
    {
      bbScene = static_cast<ImPrimitiveCube*>(p);
      break;
    }
  }

  if (!bbScene)
  {
    LOG(1, "Unable to find scene box");
    return;
  }

  Vec3 size = bbScene->size;
#endif

  vector<Vec3ui> triangles;
  vector<Vec3f> vertices;

  vec3 minPos{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
  vec3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
  for (const ImMesh* mesh : instance.scene->meshes)
  {
    if (!TrianglesFromMesh2(instance, mesh, &triangles, &vertices, &minPos, &maxPos))
      continue;
  }


  Array3f sdf;
  int gridRes = instance.options.gridSize;

  // expand the grid slightly
  vec3 mid = (minPos + maxPos) / 2;
  vec3 span = (maxPos - minPos);
  minPos = minPos - span / gridRes;
  maxPos = maxPos + span / gridRes;

  make_level_set3_brute_force(
    triangles,
    vertices,
    Vec3f{ minPos.x, minPos.y, minPos.z },
    Vec3f{ maxPos.x, maxPos.y, maxPos.z },
    Vec3i{ gridRes, gridRes, gridRes },
    sdf);

  float* ofs = sdf.a.data() + gridRes * gridRes * (gridRes - 1);
  for (size_t i = 0; i < gridRes; ++i)
  {
    for (size_t j = 0; j < gridRes; ++j)
    {
      printf("%.2f ", ofs[i*gridRes + j]);
    }
    printf("\n");
  }

  size_t oldSize = buffer.size();
  size_t dataSize = sdf.a.size() * sizeof(float);
  buffer.resize(oldSize + dataSize);
  memcpy(buffer.data() + oldSize, sdf.a.data(), dataSize);

  JsonWriter::JsonScope s(w, "sdf", JsonWriter::CompoundType::Object);
  w->Emit("dataOffset", oldSize);
  w->Emit("dataSize", dataSize);
  w->Emit("gridRes", gridRes);
  w->EmitArray("gridMin", { minPos.x, minPos.y, minPos.z });
  w->EmitArray("gridMax", { maxPos.x, maxPos.y, maxPos.z });
}

//------------------------------------------------------------------------------
void JsonExporter::CreateSDF3(JsonWriter* w)
{
  using melange::Vector;

  size_t gridRes = instance->options.gridSize;
  vector<float> sdf(gridRes*gridRes*gridRes);

  vec3 minPos = instance->scene->boundingBox.minValue;
  vec3 maxPos = instance->scene->boundingBox.maxValue;
  vec3 span = maxPos - minPos;
  minPos = minPos - 0.05f * span;
  maxPos = maxPos + 0.05f * span;

  //minPos = vec3{-200, -200, -200};
  //maxPos = vec3{+200, +200, +200};

  vec3 bottomLeft = minPos;
  vec3 cur = bottomLeft;
  vec3 inc = (maxPos - minPos) / (float)(gridRes - 1);

  for (size_t i = 0; i < gridRes; ++i)
  {
    cur.y = bottomLeft.y;
    for (size_t j = 0; j < gridRes; ++j)
    {
      cur.x = bottomLeft.x;
      for (size_t k = 0; k < gridRes; ++k)
      {
        float closestDistance = FLT_MAX;
        vec3 closestPt, closestNormal;

        for (const ImMesh* mesh : instance->scene->meshes)
        {
          const ImMeshVertex* verts = mesh->geometry.vertices.data();

          for (size_t faceIdx = 0; faceIdx < mesh->geometry.faces.size(); ++faceIdx)
          {
            const ImMeshFace& face = mesh->geometry.faces[faceIdx];
            TriangleFeature feature;
            vec3 pt = ClosestPtvec3Triangle(cur, verts[face.a], verts[face.b], verts[face.c], &feature);
            float d = LengthSq(pt - cur);
            if (d < closestDistance)
            {
              // use feature normal to determine inside/outside
              closestDistance = d;
              closestPt = pt;

              if (feature & FeatureVertex)
              {
                int ofs = feature - FeatureVertex;
                closestNormal = mesh->geometry.vertexNormals[face.vtx[ofs]];
              }
              else if (feature & FeatureEdge)
              {
                int a, b;
                if (feature == FeatureEdgeAB)
                {
                  a = face.a;
                  b = face.b;
                }
                else if (feature == FeatureEdgeAC)
                {
                  a = face.a;
                  b = face.c;
                }
                else
                {
                  // BC
                  a = face.b;
                  b = face.c;
                }
                pair<int, int> edge = make_pair(min(a, b), max(a, b));
                auto it = mesh->geometry.edgeNormals.find(edge);
                if (it != mesh->geometry.edgeNormals.end())
                {
                  closestNormal = it->second;
                }
                else
                {
                  instance->Log(1, "Unable to find edge: %d - %d\n", a, b);
                }

              }
              else
              {
                closestNormal = mesh->geometry.faceNormals[faceIdx];
              }
            }
          }
        }

        closestDistance = sqrtf(closestDistance);
        // check if the current point is behind the closest point (inside the shape)
        float dot = Dot(Normalize(cur - closestPt), closestNormal);
        float mul = dot < 0 ? -1 : 1;
        sdf[i*gridRes*gridRes + j*gridRes + k] = mul * sqrtf(closestDistance);

        cur.x += inc.x;
      }

      cur.y += inc.y;
    }

    cur.z += inc.z;
  }

  JsonWriter::JsonScope s(w, "sdf", JsonWriter::CompoundType::Object);
  AddToBuffer(sdf, "data", w);
  w->Emit("gridRes", gridRes);
  w->EmitArray("gridMin", { minPos.x, minPos.y, minPos.z });
  w->EmitArray("gridMax", { maxPos.x, maxPos.y, maxPos.z });
}
