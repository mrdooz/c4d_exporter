#include "json_exporter.hpp"
#include "json_writer.hpp"

struct Buffer
{
  string name;
  string path;
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
static vector<char> buffer;
static vector<BufferView> bufferViews;
static vector<Accessor> accessors;
static vector<Buffer> buffers;

//------------------------------------------------------------------------------
static void ExportBuffers(JsonWriter* w)
{
#define EMIT(attr) w->Emit(#attr, x.attr)

  {
    JsonWriter::JsonScope s(w, "buffers", JsonWriter::CompoundType::Object);

    for (const Buffer& x : buffers)
    {
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
      EMIT(name);
      EMIT(offset);
      EMIT(size);
    }
  }

  {
    JsonWriter::JsonScope s(w, "accessors", JsonWriter::CompoundType::Object);
    for (const Accessor& x : accessors)
    {
      JsonWriter::JsonScope s(w, x.name, JsonWriter::CompoundType::Object);
      EMIT(name);
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

  for (ImMesh* mesh : meshes)
  {
    string meshDataName = objectToNodeName[mesh] + "_mesh";
    JsonWriter::JsonScope s(w, meshDataName, JsonWriter::CompoundType::Object);

    size_t bufferViewSize = 0;
    size_t bufferViewOffset = buffer.size();

    string viewName = objectToNodeName[mesh] + "_bufferView";
    size_t accessorOffset = 0;

    // create an accessor for each data stream
    for (const ImMesh::DataStream& d : mesh->dataStreams)
    {
      bufferViewSize += d.data.size();
      size_t numElems = d.NumElems();
      size_t ofs = accessorOffset;

      switch (d.type)
      {
        case ImMesh::DataStream::Type::Index16:
        {
          accessors.push_back(Accessor{"index16", viewName, ofs, numElems, 0, "u16", "scalar", ""});
          break;
        }

        case ImMesh::DataStream::Type::Index32:
        {
          accessors.push_back(Accessor{"index32", viewName, ofs, numElems, 0, "u32", "scalar", ""});
          break;
        }

        case ImMesh::DataStream::Type::Pos:
        {
          accessors.push_back(Accessor{"pos", viewName, ofs, numElems, 0, "r32", "vec3", ""});
          break;
        }

        case ImMesh::DataStream::Type::Normal:
        {
          accessors.push_back(Accessor{"normal", viewName, ofs, numElems, 0, "r32", "vec3", ""});
          break;
        }

        case ImMesh::DataStream::Type::UV:
        {
          accessors.push_back(Accessor{"uv", viewName, ofs, numElems, 0, "r32", "vec2", ""});
          break;
        }
      }

      accessorOffset += d.data.size();
    }

    bufferViews.push_back(BufferView{"buffer", viewName, bufferViewOffset, bufferViewSize});
  }
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
bool ExportAsJson(const ImScene& scene, const Options& options, SceneStats* stats)
{
  buffer.clear();

  JsonWriter w;
  {
    JsonWriter::JsonScope s(&w, JsonWriter::CompoundType::Object);

    ExportSceneInfo(scene, &w);
    ExportNodes(scene, &w);
    ExportMeshData(scene.meshes, &w);

    ExportBuffers(&w);
  }

  return true;
}