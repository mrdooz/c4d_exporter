#include "json_exporter.hpp"
#include "json_writer.hpp"
#include "exporter_utils.hpp"
#include "sdf_gen.hpp"
#include "bit_utils.hpp"

#if WITH_EMBREE
#include <c:/projects/embree/common/math/affinespace.h>
#endif
#include "contrib/sdf/makelevelset3.h"

static unordered_map<ImBaseObject*, string> _objectToNodeName;
static vector<char> buffer;

struct StreamData
{
  const char* type;
  const char* componentType;
  size_t elementSize;
};

static unordered_map<ImMesh::DataStream::Type, StreamData> streamToStreamData = {
    {ImMesh::DataStream::Type::Index16, StreamData{"u16", "scalar", 2}},
    {ImMesh::DataStream::Type::Index32, StreamData{"u32", "scalar", 4}},
    {ImMesh::DataStream::Type::Pos, StreamData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::Normal, StreamData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::UV, StreamData{"r32", "vec2", 8}},
};

static unordered_map<ImMesh::DataStream::Type, string> streamTypeToString = {
    {ImMesh::DataStream::Type::Index16, "index"},
    {ImMesh::DataStream::Type::Index32, "index"},
    {ImMesh::DataStream::Type::Pos, "pos"},
    {ImMesh::DataStream::Type::Normal, "normal"},
    {ImMesh::DataStream::Type::UV, "uv"},
};

static unordered_map<ImLight::Type, string> lightTypeToString = {
    {ImLight::Type::Omni, "omni"},
    {ImLight::Type::Spot, "spot"},
    {ImLight::Type::Distant, "distant"},
    {ImLight::Type::Area, "area"},
};

static void ExportAnimationTracks(ImBaseObject* obj, JsonWriter* w);

//------------------------------------------------------------------------------
static void AddToBuffer(const char* data, size_t len, const string& name, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, name, JsonWriter::CompoundType::Object);
  w->Emit("offset", buffer.size());
  w->Emit("size", len);
  buffer.insert(buffer.end(), data, data + len);
}

//------------------------------------------------------------------------------
template <typename T>
static void AddToBuffer(const vector<T>& v, const string& name, JsonWriter* w)
{
  AddToBuffer((const char*)v.data(), v.size() * sizeof(T), name, w);
}

//------------------------------------------------------------------------------
static void ExportBase(ImBaseObject* obj, JsonWriter* w)
{
  w->Emit("name", obj->name);
  w->Emit("id", obj->id);

  auto fnWriteXform = [=](const string& name, const ImTransform& xform) {
    JsonWriter::JsonScope s(w, name, JsonWriter::CompoundType::Object);
    w->EmitArray("pos", {xform.pos.x, xform.pos.y, xform.pos.z});
    w->EmitArray("rot", {xform.rot.x, xform.rot.y, xform.rot.z});
    w->EmitArray("quat", {xform.quat.x, xform.quat.y, xform.quat.z, xform.quat.w});
    w->EmitArray("scale", {xform.scale.x, xform.scale.y, xform.scale.z});
  };

  fnWriteXform("xformLocal", obj->xformLocal);
  fnWriteXform("xformGlobal", obj->xformGlobal);

  ExportAnimationTracks(obj, w);

}

//------------------------------------------------------------------------------
static void ExportNullObjects(const vector<ImNullObject*>& nullObjects, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "nullObjects", JsonWriter::CompoundType::Object);

  for (ImNullObject* obj : nullObjects)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[obj], JsonWriter::CompoundType::Object);
    ExportBase(obj, w);
  }
}

//------------------------------------------------------------------------------
static void ExportCameras(const vector<ImCamera*>& cameras, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "cameras", JsonWriter::CompoundType::Object);

  for (ImCamera* cam : cameras)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[cam], JsonWriter::CompoundType::Object);
    ExportBase(cam, w);
    w->Emit("nearPlane", cam->nearPlane);
    w->Emit("farPlane", cam->farPlane);
    w->Emit("fovV", cam->verticalFov);

    if (cam->targetObj)
    {
      w->Emit("type", "target");
    }
  }
}

//------------------------------------------------------------------------------
static void ExportLights(const vector<ImLight*>& lights, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "lights", JsonWriter::CompoundType::Object);

  for (ImLight* light : lights)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[light], JsonWriter::CompoundType::Object);
    ExportBase(light, w);

    w->Emit("type", lightTypeToString[light->type]);
    switch (light->type)
    {
      case ImLight::Type::Area:
      {
        w->Emit("areaShape", light->areaShape);
        w->Emit("sizeX", light->areaSizeX);
        w->Emit("sizeY", light->areaSizeY);
        if (light->areaShape == "sphere")
          w->Emit("sizeZ", light->areaSizeZ);

        break;
      }
    }
  }
}

//------------------------------------------------------------------------------
template <typename T>
void operator+=(vector<T>& lhs, const vector<T>& rhs)
{
  lhs.insert(lhs.end(), rhs.begin(), rhs.end());
}

//------------------------------------------------------------------------------
static void ExportWorldGeometry(const ImScene& scene, JsonWriter* w)
{
  vector<ImMeshFace> faces;
  vector<ImMeshVertex> vertices;
  vector<vec3> faceNormals;
  vector<vec3> vertexNormals;

  int faceOfs = 0;
  for (ImMesh* mesh : scene.meshes)
  {
    vertices += mesh->geometry.vertices;
    vertexNormals += mesh->geometry.vertexNormals;
    faceNormals += mesh->geometry.faceNormals;

    faces.reserve(faces.size() + mesh->geometry.faces.size());
    for (const ImMeshFace& face : mesh->geometry.faces)
      faces.push_back(ImMeshFace{face.a + faceOfs, face.b + faceOfs, face.c + faceOfs});
    faceOfs += (int)mesh->geometry.vertices.size();
  }

  w->Emit("numIndices", faces.size() * 3);
  w->Emit("numVertices", vertices.size());

  AddToBuffer(faces, "indexData", w);
  AddToBuffer(vertices, "vertexData", w);
  AddToBuffer(vertexNormals, "vertexNormalData", w);
  AddToBuffer(faceNormals, "faceNormalData", w);
}

//------------------------------------------------------------------------------
static void ExportMeshData(ImMesh* mesh, JsonWriter* w)
{
  // save the stream data
  {
    JsonWriter::JsonScope s(w, "streams", JsonWriter::CompoundType::Object);
    for (const ImMesh::DataStream& dataStream : mesh->dataStreams)
    {
      JsonWriter::JsonScope s(w, streamTypeToString[dataStream.type], JsonWriter::CompoundType::Object);

      auto it = streamToStreamData.find(dataStream.type);
      if (it == streamToStreamData.end())
      {
        // LOG: unknown data stream type
        continue;
      }

      const StreamData& data = it->second;
      w->Emit("type", data.componentType);
      w->Emit("subtype", data.type);
      w->Emit("elementSize", data.elementSize);
      w->Emit("numElements", dataStream.NumElems());

      // copy the stream data to the buffer
      AddToBuffer(dataStream.data, "data", w);
    }
  }

  {
    // write the materialGroups
    JsonWriter::JsonScope s(w, "materialGroups", JsonWriter::CompoundType::Array);
    for (const ImMesh::MaterialGroup& m : mesh->materialGroups)
    {
      JsonWriter::JsonScope s(w, JsonWriter::CompoundType::Object);
      w->Emit("materialId", m.materialId);
      w->Emit("startIndex", m.startIndex);
      w->Emit("indexCount", m.indexCount);
    }
  }
}

//------------------------------------------------------------------------------
static void ExportAnimationTracks(ImBaseObject* obj, JsonWriter* w)
{
  if (obj->sampledAnimTracks.empty())
    return;

  {
    JsonWriter::JsonScope s(w, "animTracks", JsonWriter::CompoundType::Object);

    for (ImSampledTrack& track : obj->sampledAnimTracks)
    {
      JsonWriter::JsonScope s(w, track.name, JsonWriter::CompoundType::Object);

      // determine best encoding
      float minValue = +FLT_MAX;
      float maxValue = -FLT_MAX;

      for (float v : track.values)
      {
        minValue = min(minValue, v);
        maxValue = max(maxValue, v);
      }

      float span = maxValue - minValue;

      for (size_t i = 0; i < track.values.size(); ++i)
      {
        track.values[i] = (track.values[i] - minValue) / span;
      }

      float maxErr = 0.0001f;
      u32 numBits = 8;
      for (; numBits < 32; ++numBits)
      {
        bool precisionError = false;
        for (float v : track.values)
        {
          int m = 1 << (numBits - 1);
          float err = fabs(v - (float)(int(m * v)) / m);
          if (err > maxErr)
          {
            precisionError = true;
            break;
          }
        }

        if (!precisionError)
          break;
      }

      vector<u8> data;
      BitWriter writer;
      for (float v : track.values)
      {
        int m = 1 << (numBits - 1);
        u32 vv = u32(m * v);
        writer.Write(vv, numBits);
      }

      w->Emit("fps", g_scene.fps);
      w->Emit("numKeys", track.values.size());
      w->Emit("minValue", minValue);
      w->Emit("maxValue", maxValue);
      w->Emit("bitLength", numBits);
      writer.CopyOut(&data);

      AddToBuffer(data, "data", w);
    }
  }
}

//------------------------------------------------------------------------------
static void ExportMeshes(const vector<ImMesh*>& meshes, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "meshes", JsonWriter::CompoundType::Object);

  for (ImMesh* mesh : meshes)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[mesh], JsonWriter::CompoundType::Object);

    ExportBase(mesh, w);
    ExportMeshData(mesh, w);
    {
      JsonWriter::JsonScope s(w, "boundingSphere", JsonWriter::CompoundType::Object);
      w->Emit("radius", mesh->boundingSphere.radius);
      auto& center = mesh->boundingSphere.center;
      w->EmitArray("center", { center.x, center.y, center.z });
    }

    {
      JsonWriter::JsonScope s(w, "boundingBox", JsonWriter::CompoundType::Object);
      const vec3& center = (mesh->aabb.maxValue + mesh->aabb.minValue) / 2;
      const vec3& extents = (mesh->aabb.maxValue - mesh->aabb.minValue) / 2;
      w->EmitArray("center", { center.x, center.y, center.z });
      w->EmitArray("extents", { extents.x, extents.y, extents.z });
    }
  }
}

//------------------------------------------------------------------------------
static void ExportPrimitives(const vector<ImPrimitive*>& primitives, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "primitives", JsonWriter::CompoundType::Object);

  for (ImPrimitive* prim : primitives)
  {
    switch (prim->type)
    {
      case ImPrimitive::Type::Cube:
      {
        const ImPrimitiveCube* c = static_cast<const ImPrimitiveCube*>(prim);
        JsonWriter::JsonScope s(w, "cube", JsonWriter::CompoundType::Object);
        ExportBase(prim, w);
        w->EmitArray("size", {c->size.x, c->size.y, c->size.z});
        break;
      }
    }
  }
}

//------------------------------------------------------------------------------
static void ExportSceneInfo(const ImScene& scene, JsonWriter* w)
{
  vector<ImBaseObject*> allObjects;

  JsonWriter::JsonScope s(w, "scene", JsonWriter::CompoundType::Object);
  {
    unordered_map<string, int> nodeIdx;

    auto& fnAddElem = [&nodeIdx, w, &allObjects](const char* base, ImBaseObject* obj) {
      char name[32];
      sprintf(name, "%s%.5d", base, ++nodeIdx[base]);
      _objectToNodeName[obj] = name;
      allObjects.push_back(obj);
    };

    for (ImBaseObject* obj : scene.nullObjects)
      fnAddElem("Null", obj);

    for (ImBaseObject* obj : scene.cameras)
      fnAddElem("Camera", obj);

    for (ImBaseObject* obj : scene.lights)
      fnAddElem("Light", obj);

    for (ImBaseObject* obj : scene.meshes)
      fnAddElem("Mesh", obj);
  }

  {
    JsonWriter::JsonScope s(w, "nodes", JsonWriter::CompoundType::Object);
    for (ImBaseObject* obj : allObjects)
    {
      JsonWriter::JsonScope s(w, _objectToNodeName[obj], JsonWriter::CompoundType::Object);
      vector<string> children;
      for (ImBaseObject* obj : obj->children)
        children.push_back(obj->name);
      w->EmitArray("children", children);
    }
  }

  w->Emit("buffer", options.outputBase + ".dat");
  {
    JsonWriter::JsonScope s(w, "geometry", JsonWriter::CompoundType::Object);
    ExportWorldGeometry(scene, w);
  }
}

//------------------------------------------------------------------------------
static void ExportMaterials(const vector<ImMaterial*>& materials, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "materials", JsonWriter::CompoundType::Object);

  for (const ImMaterial* material : materials)
  {
    JsonWriter::JsonScope s(w, material->name, JsonWriter::CompoundType::Object);

    w->Emit("name", material->name);
    w->Emit("id", material->id);

    JsonWriter::JsonScope s2(w, "components", JsonWriter::CompoundType::Object);

    for (const ImMaterialComponent& comp : material->components)
    {
      JsonWriter::JsonScope s(w, comp.name, JsonWriter::CompoundType::Object);
      w->EmitArray("color", {comp.color.r, comp.color.g, comp.color.b});
      if (!comp.texture.empty())
        w->Emit("texture", comp.texture);
      w->Emit("brightness", comp.brightness);
    }
  }
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
  vec3 orgPos = {-size.x / 2 + inc.x / 2, -size.y / 2 + inc.y / 2, -size.z / 2 + inc.z / 2};
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
static bool TrianglesFromMesh2(const ImMesh* mesh,
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
    LOG(1, "Unable to find index or pos stream for mesh: %s", mesh->name.c_str());
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
static void CreateSDF2(const ImScene& scene, const Options& options, JsonWriter* w)
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
  for (const ImMesh* mesh : scene.meshes)
  {
    if (!TrianglesFromMesh2(mesh, &triangles, &vertices, &minPos, &maxPos))
      continue;
  }

  
  Array3f sdf;
  int gridRes = options.gridSize;

  // expand the grid slightly
  vec3 mid = (minPos + maxPos) / 2;
  vec3 span = (maxPos - minPos);
  minPos = minPos - span / gridRes;
  maxPos = maxPos + span / gridRes;

  make_level_set3_brute_force(
      triangles,
      vertices,
      Vec3f{minPos.x, minPos.y, minPos.z},
      Vec3f{maxPos.x, maxPos.y, maxPos.z},
      Vec3i{gridRes, gridRes, gridRes},
      sdf);

  float* ofs = sdf.a.data() + gridRes * gridRes * (gridRes - 1);
  for (size_t i = 0; i < gridRes; ++i)
  {
    for (size_t j = 0; j < gridRes; ++j)
    {
      printf("%.2f ", ofs[i*gridRes+j]);
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
  w->EmitArray("gridMin", {minPos.x, minPos.y, minPos.z});
  w->EmitArray("gridMax", {maxPos.x, maxPos.y, maxPos.z});
}

//------------------------------------------------------------------------------
static void CreateSDF3(const ImScene& scene, const Options& options, JsonWriter* w)
{
  using melange::Vector;

  size_t gridRes = options.gridSize;
  vector<float> sdf(gridRes*gridRes*gridRes);

  vec3 minPos = scene.boundingBox.minValue;
  vec3 maxPos = scene.boundingBox.maxValue;
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

        for (const ImMesh* mesh : scene.meshes)
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
                  LOG(1, "Unable to find edge: %d - %d\n", a, b);
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

//------------------------------------------------------------------------------
bool ExportAsJson(const ImScene& scene, const Options& options, SceneStats* stats)
{
  JsonWriter w;
  {
    JsonWriter::JsonScope s(&w, JsonWriter::CompoundType::Object);

    ExportSceneInfo(scene, &w);

    ExportNullObjects(scene.nullObjects, &w);
    ExportCameras(scene.cameras, &w);
    ExportLights(scene.lights, &w);
    ExportMeshes(scene.meshes, &w);
    ExportMaterials(scene.materials, &w);
    ExportPrimitives(scene.primitives, &w);

    if (options.sdf)
    {
      //CreateSDF(scene, options, &w);
      //CreateSDF2(scene, options, &w);
      CreateSDF3(scene, options, &w);
    }
  }

  // save the json file
  FILE* f = fopen(string(options.outputPrefix + ".json").c_str(), "wt");
  if (f)
  {
    fputs(w.res.c_str(), f);
    fclose(f);
  }

  // save the data buffer
  if (buffer.size() > 0)
  {
    f = fopen(string(options.outputPrefix + ".dat").c_str(), "wb");
    if (f)
    {
      fwrite(buffer.data(), buffer.size(), 1, f);
      fclose(f);
    }
  }

  return true;
}
