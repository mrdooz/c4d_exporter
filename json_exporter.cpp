#include "json_exporter.hpp"
#include "json_writer.hpp"
#include "exporter_utils.hpp"
#if WITH_EMBREE
#include <c:/projects/embree/common/math/affinespace.h>
#endif
#include "contrib/sdf/makelevelset3.h"

struct BufferView
{
  string name;
  size_t offset;
  size_t size;
};

struct Accessor
{
  string name;
  string bufferView;
  size_t offset;
  size_t count;
  size_t stride;
  size_t elementSize;

  string type;
  string componentType;

  string compression;
};

static unordered_map<ImBaseObject*, string> objectToNodeName;
static unordered_map<ImMesh*, int> meshCountByMesh;

static vector<BufferView> bufferViews;
static vector<Accessor> accessors;
static vector<char> buffer;

struct AccessorData
{
  const char* type;
  const char* componentType;
  size_t elementSize;
};

static unordered_map<ImMesh::DataStream::Type, AccessorData> streamToAccessor = {
    {ImMesh::DataStream::Type::Index16, AccessorData{"u16", "scalar", 2}},
    {ImMesh::DataStream::Type::Index32, AccessorData{"u32", "scalar", 4}},
    {ImMesh::DataStream::Type::Pos, AccessorData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::Normal, AccessorData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::UV, AccessorData{"r32", "vec2", 8}},
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

//------------------------------------------------------------------------------
static void ExportBuffers(const Options& options, JsonWriter* w)
{
#define EMIT(attr) w->Emit(#attr, x.attr)

  {
    JsonWriter::JsonScope s(w, "bufferViews", JsonWriter::CompoundType::Object);

    w->Emit("buffer", options.outputBase + ".dat");

    for (const BufferView& x : bufferViews)
    {
      JsonWriter::JsonScope s(w, x.name, JsonWriter::CompoundType::Object);
      EMIT(offset);
      EMIT(size);
    }
  }

  {
    JsonWriter::JsonScope s(w, "accessors", JsonWriter::CompoundType::Object);
    for (const Accessor& x : accessors)
    {
      JsonWriter::JsonScope s(w, x.name, JsonWriter::CompoundType::Object);
      EMIT(bufferView);
      EMIT(offset);
      EMIT(count);
      if (x.stride)
        EMIT(stride);
      EMIT(elementSize);
      EMIT(type);
      EMIT(componentType);
      if (!x.compression.empty())
        EMIT(compression);
    }
  }
#undef EMIT
}

//------------------------------------------------------------------------------
static void ExportBase(const ImBaseObject* obj, JsonWriter* w)
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
}

//------------------------------------------------------------------------------
static void ExportNullObjects(const vector<ImNullObject*>& nullObjects, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "nullObjects", JsonWriter::CompoundType::Object);

  for (ImNullObject* obj : nullObjects)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[obj], JsonWriter::CompoundType::Object);
    ExportBase(obj, w);
  }
}

//------------------------------------------------------------------------------
static void ExportCameras(const vector<ImCamera*>& cameras, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "cameras", JsonWriter::CompoundType::Object);

  for (ImCamera* cam : cameras)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[cam], JsonWriter::CompoundType::Object);
    ExportBase(cam, w);

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
    JsonWriter::JsonScope s(w, objectToNodeName[light], JsonWriter::CompoundType::Object);
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
static void ExportMeshData(ImMesh* mesh, JsonWriter* w)
{
  size_t bufferViewSize = 0;
  size_t bufferViewOffset = buffer.size();

  string viewName = objectToNodeName[mesh] + "_bufferView";
  size_t accessorOffset = 0;

  unordered_map<ImMesh::DataStream::Type, string> dataStreamToAccessor;

  // create an accessor for each data stream, and copy the stream data to the buffer
  for (const ImMesh::DataStream& dataStream : mesh->dataStreams)
  {
    bufferViewSize += dataStream.data.size();
    size_t numElems = dataStream.NumElems();
    char name[32];
    sprintf(name, "Accessor%.5d", (int)accessors.size() + 1);

    auto it = streamToAccessor.find(dataStream.type);
    if (it == streamToAccessor.end())
    {
      // LOG: unknown data stream type
      continue;
    }

    const AccessorData& data = it->second;
    accessors.push_back(Accessor{name,
        viewName,
        accessorOffset,
        numElems,
        0u,
        data.elementSize,
        data.type,
        data.componentType,
        ""});

    // copy the stream data to the buffer
    buffer.insert(buffer.end(), dataStream.data.begin(), dataStream.data.end());
    dataStreamToAccessor[dataStream.type] = name;

    accessorOffset += dataStream.data.size();
  }

  {
    // write the bindings
    JsonWriter::JsonScope s(w, "bindings", JsonWriter::CompoundType::Object);
    for (auto& kv : dataStreamToAccessor)
    {
      w->Emit(streamTypeToString[kv.first], kv.second);
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

  bufferViews.push_back(BufferView{viewName, bufferViewOffset, bufferViewSize});
}

//------------------------------------------------------------------------------
static void ExportMeshes(const vector<ImMesh*>& meshes, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "meshes", JsonWriter::CompoundType::Object);

  for (ImMesh* mesh : meshes)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[mesh], JsonWriter::CompoundType::Object);

    {
      JsonWriter::JsonScope s(w, "bounding_sphere", JsonWriter::CompoundType::Object);
      w->Emit("radius", mesh->boundingSphere.radius);
      auto& center = mesh->boundingSphere.center;
      w->EmitArray("center", {center.x, center.y, center.z});
    }

    {
      JsonWriter::JsonScope s(w, "bounding_box", JsonWriter::CompoundType::Object);
      auto& center = mesh->aabb.center;
      auto& extents = mesh->aabb.extents;
      w->EmitArray("center", {center.x, center.y, center.z});
      w->EmitArray("extents", {extents.x, extents.y, extents.z});
    }

    ExportBase(mesh, w);
    ExportMeshData(mesh, w);
  }
}

//------------------------------------------------------------------------------
static void ExportPrimitives(const vector<ImPrimitive*>& primitives, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "primitives", JsonWriter::CompoundType::Object);

  for (const ImPrimitive* prim : primitives)
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
      objectToNodeName[obj] = name;
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
      JsonWriter::JsonScope s(w, objectToNodeName[obj], JsonWriter::CompoundType::Object);
      vector<string> children;
      for (ImBaseObject* obj : obj->children)
        children.push_back(obj->name);
      w->EmitArray("children", children);
    }
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
static bool TrianglesFromMesh(const ImMesh* mesh, vector<ImMeshTriangle>* triangles, vector<ImMeshVertex>* vertices)
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

  Vec3* vtx = (Vec3*)posStream->data.data();
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

  vector<Vec3> triangleCenters;

  // Add all the meshes to the embree scene
  for (const ImMesh* mesh : scene.meshes)
  {
    vector<ImMeshTriangle> triangles;
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
      const ImMeshVertex& v0 = vertices[triangles[i].v0];
      const ImMeshVertex& v1 = vertices[triangles[i].v1];
      const ImMeshVertex& v2 = vertices[triangles[i].v2];

      Vector center =
          (Vector(v0.x, v0.y, v0.z) + Vector(v1.x, v1.y, v1.z) + Vector(v2.x, v2.y, v2.z)) / 3;

      center = mesh->xformGlobal.mtx * center;
      triangleCenters.push_back(Vec3{ (float)center.x, (float)center.y, (float)center.z });
    }

    ImMeshVertex* rtcVertices = (ImMeshVertex*)rtcMapBuffer(rtcScene, geomId, RTC_VERTEX_BUFFER);
    memcpy(rtcVertices, vertices.data(), vertices.size() * sizeof(ImMeshVertex));
    rtcUnmapBuffer(rtcScene, geomId, RTC_VERTEX_BUFFER);

    ImMeshTriangle* rtcTriangles = (ImMeshTriangle*)rtcMapBuffer(rtcScene, geomId, RTC_INDEX_BUFFER);
    memcpy(rtcTriangles, triangles.data(), triangles.size() * sizeof(ImMeshTriangle));
    rtcUnmapBuffer(rtcScene, geomId, RTC_INDEX_BUFFER);
  }

  rtcCommit(rtcScene);

  // Traverse the scene grid, and find the closest point

  int gridSize = options.gridSize;

  Vec3 size = bbScene->size;
  Vec3 inc = { size.x / gridSize, size.y / gridSize, size.z / gridSize };
  Vec3 orgPos = {-size.x / 2 + inc.x / 2, -size.y / 2 + inc.y / 2, -size.z / 2 + inc.z / 2};
  Vec3 curPos = orgPos;

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

          Vec3 dir = Normalize(triangleCenters[n] - curPos);
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
    Vec3* minPos,
    Vec3* maxPos)
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

  Vec3* vtx = (Vec3*)posStream->data.data();
  for (size_t i = 0; i < numVertices; ++i)
  {
    melange::Vector v = melange::Vector{ vtx->x, vtx->y, vtx->z };
    v = mesh->xformGlobal.mtx * v;
    Vec3 vv = { (float)v.x, (float)v.y, (float)v.z };
    (*vertices)[i + oldVtxIdx] = Vec3f{ vv.x, vv.y, vv.z };
    *minPos = Vec3{ min(vv.x, minPos->x), min(vv.y, minPos->y), min(vv.z, minPos->z) };
    *maxPos = Vec3{ max(vv.x, maxPos->x), max(vv.y, maxPos->y), max(vv.z, maxPos->z) };
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

  Vec3 minPos{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
  Vec3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
  for (const ImMesh* mesh : scene.meshes)
  {
    if (!TrianglesFromMesh2(mesh, &triangles, &vertices, &minPos, &maxPos))
      continue;
  }

  
  Array3f sdf;
  int gridRes = options.gridSize;
  Vec3 boxSize = maxPos - minPos;
  int cx = boxSize.x / gridRes;
  int cy = boxSize.y / gridRes;
  int cz = boxSize.z / gridRes;

  make_level_set3_brute_force(
      triangles,
      vertices,
      Vec3f{minPos.x, minPos.y, minPos.z},
      Vec3f{maxPos.x, maxPos.y, maxPos.z},
      Vec3i{gridRes, gridRes, gridRes},
      sdf);

  float* ofs = sdf.a.data + gridRes * gridRes * (gridRes - 1);
  for (size_t i = 0; i < gridRes; ++i)
  {
    for (size_t j = 0; j < gridRes; ++j)
    {
      printf("%.2f ", ofs[i*gridRes+j]);
    }
    printf("\n");
  }

  size_t oldSize = buffer.size();
  size_t dataSize = sdf.a.n * sizeof(float);
  buffer.resize(oldSize + dataSize);
  memcpy(buffer.data() + oldSize, sdf.a.data, dataSize);

  JsonWriter::JsonScope s(w, "sdf", JsonWriter::CompoundType::Object);
  w->Emit("dataOffset", oldSize);
  w->Emit("dataSize", dataSize);
  w->Emit("gridRes", gridRes);
  w->EmitArray("gridSize", { cx, cy, cz });
  w->EmitArray("gridMin", {minPos.x, minPos.y, minPos.z});
  w->EmitArray("gridMax", {maxPos.x, maxPos.y, maxPos.z});
  //w->EmitArray("gridBox", sdf.a.
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

    ExportBuffers(options, &w);

    if (options.sdf)
    {
      //CreateSDF(scene, options, &w);
      CreateSDF2(scene, options, &w);
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
