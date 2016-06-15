#include "json_exporter.hpp"
#include "json_writer.hpp"

struct Buffer
{
  string name;
  string path;
  vector<char> data;
};

struct BufferView
{
  string buffer;
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

  string type;
  string componentType;

  string compression;
};

static unordered_map<ImBaseObject*, string> objectToNodeName;
static unordered_map<ImMesh*, int> meshCountByMesh;

static vector<BufferView> bufferViews;
static vector<Accessor> accessors;
static vector<Buffer> buffers;

static unordered_map<ImMesh::DataStream::Type, pair<string, string>> streamToAccessor = {
  { ImMesh::DataStream::Type::Index16, make_pair("u16", "scalar") },
  { ImMesh::DataStream::Type::Index32, make_pair("u32", "scalar") },
  { ImMesh::DataStream::Type::Pos, make_pair("r32", "vec3") },
  { ImMesh::DataStream::Type::Normal, make_pair("r32", "vec3") },
  { ImMesh::DataStream::Type::UV, make_pair("r32", "vec2") },
};

static unordered_map<ImMesh::DataStream::Type, string> streamTypeToString = {
  { ImMesh::DataStream::Type::Index16, "index" },
  { ImMesh::DataStream::Type::Index32, "index" },
  { ImMesh::DataStream::Type::Pos, "pos" },
  { ImMesh::DataStream::Type::Normal, "normal" },
  { ImMesh::DataStream::Type::UV, "uv" },
};

static unordered_map<ImLight::Type, string> lightTypeToString = {
  { ImLight::Type::Omni, "omni" },
  { ImLight::Type::Spot, "spot" },
  { ImLight::Type::Distant, "distant" },
  { ImLight::Type::Area, "area" },
};

//------------------------------------------------------------------------------
static void ExportBuffers(const Options& options, JsonWriter* w)
{
#define EMIT(attr) w->Emit(#attr, x.attr)

  {
    JsonWriter::JsonScope s(w, "buffers", JsonWriter::CompoundType::Object);

    for (Buffer& x : buffers)
    {
      x.path = options.outputBase + ".dat";

      JsonWriter::JsonScope s(w, x.name, JsonWriter::CompoundType::Object);
      EMIT(name);
      EMIT(path);
    }
  }

  {
    JsonWriter::JsonScope s(w, "bufferViews", JsonWriter::CompoundType::Object);

    for (const BufferView& x : bufferViews)
    {
      JsonWriter::JsonScope s(w, x.name, JsonWriter::CompoundType::Object);
      EMIT(buffer);
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
  
  vector<string> children;
  for (ImBaseObject* obj : obj->children)
    children.push_back(obj->name);

  auto fnWriteXform = [=](const string& name, const ImTransform& xform)
  {
    JsonWriter::JsonScope s(w, name, JsonWriter::CompoundType::Object);
    w->EmitArray("pos", { xform.pos.x, xform.pos.y, xform.pos.z });
    w->EmitArray("rot", { xform.rot.x, xform.rot.y, xform.rot.z });
    w->EmitArray("scale", { xform.scale.x, xform.scale.y, xform.scale.z });
  };

  fnWriteXform("xformLocal", obj->xformLocal);
  fnWriteXform("xformGlobal", obj->xformGlobal);

  w->EmitArray("children", children);
}

//------------------------------------------------------------------------------
static void ExportNullObjects(const vector<ImNullObject*>& nullObjects, JsonWriter* w)
{
  for (ImNullObject* obj : nullObjects)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[obj], JsonWriter::CompoundType::Object);
    ExportBase(obj, w);
  }
}

//------------------------------------------------------------------------------
static void ExportCameras(const vector<ImCamera*>& cameras, JsonWriter* w)
{
  for (ImCamera* cam : cameras)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[cam], JsonWriter::CompoundType::Object);
    ExportBase(cam, w);
  }
}

//------------------------------------------------------------------------------
static void ExportLights(const vector<ImLight*>& lights, JsonWriter* w)
{
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
static void ExportMeshes(const vector<ImMesh*>& meshes, JsonWriter* w)
{
  for (ImMesh* mesh : meshes)
  {
    JsonWriter::JsonScope s(w, objectToNodeName[mesh], JsonWriter::CompoundType::Object);
    ExportBase(mesh, w);

    // gltf wants us to just write the mesh names here, and have them separate..
    {
      JsonWriter::JsonScope s(w, "meshes", JsonWriter::CompoundType::Array);

      // XXX(magnus): just a single mesh per mesh node for now
      w->EmitArrayElem(objectToNodeName[mesh] + "_mesh");
    }

  }
}

//------------------------------------------------------------------------------
static void ExportMeshData(const vector<ImMesh*>& meshes, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "meshes", JsonWriter::CompoundType::Object);

  vector<char> buffer;

  for (ImMesh* mesh : meshes)
  {
    string meshDataName = objectToNodeName[mesh] + "_mesh";
    JsonWriter::JsonScope s(w, meshDataName, JsonWriter::CompoundType::Object);

    size_t bufferViewSize = 0;
    size_t bufferViewOffset = buffer.size();

    string viewName = objectToNodeName[mesh] + "_bufferView";
    size_t accessorOffset = 0;

    unordered_map<ImMesh::DataStream::Type, string> dataStreamToAccessor;

    // create an accessor for each data stream
    for (const ImMesh::DataStream& d : mesh->dataStreams)
    {
      bufferViewSize += d.data.size();
      size_t numElems = d.NumElems();
      size_t ofs = accessorOffset;
      char name[32];
      sprintf(name, "Accessor%.5d", (int)accessors.size() + 1);

      auto it = streamToAccessor.find(d.type);
      if (it == streamToAccessor.end())
      {
        // LOG: unknown data stream type
        continue;
      }

      const auto& data = it->second;
      accessors.push_back(Accessor{ name, viewName, ofs, numElems, 0, data.first, data.second, "" });
      buffer.insert(buffer.end(), d.data.begin(), d.data.end());
      dataStreamToAccessor[d.type] = name;

      accessorOffset += d.data.size();
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
      JsonWriter::JsonScope s(w, "materialGroups", JsonWriter::CompoundType::Object);
      for (const ImMesh::MaterialGroup& m : mesh->materialGroups)
      {
        w->Emit("materialId", m.materialId);
        w->Emit("startIndex", m.startIndex);
        w->Emit("indexCount", m.indexCount);
      }
    }

    bufferViews.push_back(BufferView{"buffer", viewName, bufferViewOffset, bufferViewSize});
  }

  buffers.push_back(Buffer{"buffer", "", buffer});
}

//------------------------------------------------------------------------------
static void ExportNodes(const ImScene& scene, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "nodes", JsonWriter::CompoundType::Object);
  ExportNullObjects(scene.nullObjects, w);
  ExportCameras(scene.cameras, w);
  ExportLights(scene.lights, w);
  ExportMeshes(scene.meshes, w);
}

//------------------------------------------------------------------------------
static void ExportSceneInfo(const ImScene& scene, JsonWriter* w)
{
  w->Emit("scene", "defaultScene");
  JsonWriter::JsonScope s(w, "scenes", JsonWriter::CompoundType::Object);
  {
    JsonWriter::JsonScope s(w, "defaultScene", JsonWriter::CompoundType::Object);

    {
      JsonWriter::JsonScope s(w, "nodes", JsonWriter::CompoundType::Array);
      unordered_map<string, int> nodeIdx;

      auto& fnAddElem = [&nodeIdx, w](const char* base, ImBaseObject* obj)
      {
        char name[32];
        sprintf(name, "%s%.5d", base, ++nodeIdx[base]);
        w->EmitArrayElem(name);
        objectToNodeName[obj] = name;
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
  }
}

//------------------------------------------------------------------------------
bool ExportAsJsonGltf(const ImScene& scene, const Options& options, SceneStats* stats)
{
  JsonWriter w;
  {
    JsonWriter::JsonScope s(&w, JsonWriter::CompoundType::Object);

    ExportSceneInfo(scene, &w);
    ExportNodes(scene, &w);
    ExportMeshData(scene.meshes, &w);

    ExportBuffers(options, &w);
  }

  FILE* f = fopen(string(options.outputPrefix + ".json").c_str(), "wt");
  if (f)
  {
    fputs(w.res.c_str(), f);
    fclose(f);
  }

  if (buffers.size() > 0)
  {
    f = fopen(string(options.outputPrefix + ".dat").c_str(), "wb");
    if (f)
    {
      const vector<char>& buf = buffers.front().data;
      fwrite(buf.data(), buf.size(), 1, f);
      fclose(f);
    }
  }

  return true;
}