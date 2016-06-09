#include "boba_scene_format.hpp"
#include "deferred_writer.hpp"
#include "exporter.hpp"
#include "save_scene.hpp"

#include "compress/forsythtriangleorderoptimizer.h"
#include "compress/indexbuffercompression.h"
#include "compress/indexbufferdecompression.h"
#include "compress/oct.h"

namespace exporter
{
  //------------------------------------------------------------------------------
  static vector<int> CreateFixupRange(int count, DeferredWriter& w)
  {
    vector<int> res;
    for (int i = 0; i < count; ++i)
      res.push_back(w.CreateFixup());
    return res;
  }

  //------------------------------------------------------------------------------
  struct ScopedStats
  {
    ScopedStats(const DeferredWriter& writer, int* val) : writer(writer), val(val)
    {
      *val = writer.GetFilePos();
    }
    ~ScopedStats() { *val = -(*val - writer.GetFilePos()); }
    const DeferredWriter& writer;
    int* val;
  };

  //------------------------------------------------------------------------------
  bool SaveScene(const ImScene& scene, const Options& options, SceneStats* stats)
  {
    DeferredWriter writer;
    if (!writer.Open(options.outputFilename.c_str()))
      return false;

    protocol::SceneBlob header;
    memset(&header, 0, sizeof(header));
    header.id[0] = 'b';
    header.id[1] = 'o';
    header.id[2] = 'b';
    header.id[3] = 'a';

    header.flags = 0;
    header.version = BOBA_PROTOCOL_VERSION;

    // dummy write the header
    writer.Write(header);

    {
      ScopedStats s(writer, &stats->nullObjectSize);
      header.numNullObjects = (u32)scene.nullObjects.size();
      header.nullObjectDataStart = header.numNullObjects ? (u32)writer.GetFilePos() : 0;
      for (ImNullObject* obj : scene.nullObjects)
      {
        SaveNullObject(obj, options, writer);
      }
    }

  {
    ScopedStats s(writer, &stats->meshSize);
    header.numMeshes = (u32)scene.meshes.size();
    header.meshDataStart = header.numMeshes ? (u32)writer.GetFilePos() : 0;
    vector<int> fixups = CreateFixupRange(header.numMeshes, writer);
    for (int i = 0; i < (int)scene.meshes.size(); ++i)
    {
      ImMesh* mesh = scene.meshes[i];
      writer.InsertFixup(fixups[i]);
      SaveMesh(mesh, options, writer);
    }
  }

  {
    ScopedStats s(writer, &stats->lightSize);
    header.numLights = (u32)scene.lights.size();
    header.lightDataStart = header.numLights ? (u32)writer.GetFilePos() : 0;
    for (const ImLight* light : scene.lights)
    {
      SaveLight(light, options, writer);
    }
  }

  {
    ScopedStats s(writer, &stats->cameraSize);
    header.numCameras = (u32)scene.cameras.size();
    header.cameraDataStart = header.numCameras ? (u32)writer.GetFilePos() : 0;
    for (const ImCamera* camera : scene.cameras)
    {
      SaveCamera(camera, options, writer);
    }
  }

  {
    ScopedStats s(writer, &stats->materialSize);
    header.numMaterials = (u32)scene.materials.size();
    header.materialDataStart = header.numMaterials ? (u32)writer.GetFilePos() : 0;
    for (const ImMaterial* material : scene.materials)
    {
      SaveMaterial(material, options, writer);
    }
  }

  {
    ScopedStats s(writer, &stats->splineSize);
    header.numSplines = (u32)scene.splines.size();
    header.splineDataStart = header.numSplines ? (u32)writer.GetFilePos() : 0;
    for (const ImSpline* spline : scene.splines)
    {
      SaveSpline(spline, options, writer);
    }
  }

  {
    ScopedStats s(writer, &stats->dataSize);
    header.fixupOffset = (u32)writer.GetFilePos();
    writer.WriteDeferredData();
  }

  // write back the correct header
  writer.SetFilePos(0);
  writer.Write(header);

  return true;
  }

  //------------------------------------------------------------------------------
  void SaveMaterial(const ImMaterial* material, const Options& options, DeferredWriter& writer)
  {
    writer.StartBlockMarker();

    writer.AddDeferredString(material->name);
    writer.Write(material->id);

    int componentFixup = writer.CreateFixup();

    // write the components
    // first # components, then the actual components
    writer.InsertFixup(componentFixup);
    int numComponents = (int)material->components.size();
    writer.Write(numComponents);
    vector<int> componentFixups = CreateFixupRange(numComponents, writer);

    for (int i = 0; i < numComponents; ++i)
    {
      writer.InsertFixup(componentFixups[i]);
      const ImMaterialComponent& c = material->components[i];

      writer.AddDeferredString(c.name);
      writer.Write(c.color);
      writer.AddDeferredString(c.texture);
      writer.Write(c.brightness);
    }

    writer.EndBlockMarker();
  }

  //------------------------------------------------------------------------------
  void SaveBase(const ImBaseObject* base, const Options& options, DeferredWriter& writer)
  {
    writer.AddDeferredString(base->name);
    writer.Write(base->id);
    writer.Write(base->parent ? base->parent->id : protocol::INVALID_OBJECT_ID);
#if BOBA_PROTOCOL_VERSION < 4
    writer.Write(base->mtxLocal);
    writer.Write(base->mtxGlobal);
#endif
#if BOBA_PROTOCOL_VERSION >= 4
    writer.Write(base->xformLocal);
    writer.Write(base->xformGlobal);
#endif
  }

#if 0
  //------------------------------------------------------------------------------
  void SaveCompressedVertices(Mesh* mesh, const Options& options, DeferredWriter& writer)
  {
    {
      // normals
      size_t numNormals = mesh->normals.size() / 3;
      vector<s16> compressedNormals(numNormals * 2);

      auto Normalize = [](float* v) {
        float x = v[0];
        float y = v[1];
        float z = v[2];
        float len = sqrt(x * x + y * y + z * z);
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
      };

      for (size_t i = 0; i < numNormals; ++i)
      {
        Snorm<snormSize> res[2];
        Normalize(mesh->normals.data() + i * 3);
        octPEncode(&mesh->normals[i * 3], res);
        compressedNormals[i * 2 + 0] = res[0].bits() & 0xffff;
        compressedNormals[i * 2 + 1] = res[1].bits() & 0xffff;
      }

      writer.AddDeferredVector(compressedNormals);
    }

    {
      // texture coords
      size_t numUvs = mesh->uv.size();
      vector<s16> compressedUV(numUvs);
      for (size_t i = 0; i < numUvs / 2; ++i)
      {
        Snorm<snormSize> u(mesh->uv[i * 2 + 0]);
        Snorm<snormSize> v(mesh->uv[i * 2 + 1]);
        compressedUV[i * 2 + 0] = u.bits() & 0xffff;
        compressedUV[i * 2 + 1] = v.bits() & 0xffff;
      }
      writer.AddDeferredVector(compressedUV);
    }

    {
      // indices
      WriteBitstream output;
      vector<u32> vertexRemap(mesh->verts.size() / 3);
      CompressIndexBuffer((const u32*)mesh->indices.data(),
        (u32)mesh->indices.size() / 3,
        vertexRemap.data(),
        (u32)vertexRemap.size(),
        IBCF_AUTO,
        output);
      // TODO: remap the vertices
      vector<u8> compressedIndices(output.ByteSize());
      writer.AddDeferredVector(compressedIndices);
    }
  }

  //------------------------------------------------------------------------------
  void OptimizeFaces(Mesh* mesh)
  {
    vector<int> optimizedIndices(mesh->indices.size());
    Forsyth::OptimizeFaces((const u32*)mesh->indices.data(),
      (u32)mesh->indices.size(),
      (u32)mesh->verts.size() / 3,
      (u32*)optimizedIndices.data(),
      32);
    mesh->indices.swap(optimizedIndices);
  }
#endif

  //------------------------------------------------------------------------------
  void SaveMesh(ImMesh* mesh, const Options& options, DeferredWriter& writer)
  {
    SaveBase(mesh, options, writer);

    // save bounding volume
    writer.Write(mesh->boundingSphere);

    int materialGroupFixup = writer.CreateFixup();

    // write the data streams fixup. this is essentially saving a pointer
    // to an array of pointers :)
    int streamFixup = writer.CreateFixup();

    // write material groups
    writer.InsertFixup(materialGroupFixup);
    int numMaterialGroups = (int)mesh->materialGroups.size();
    writer.Write(numMaterialGroups);
    vector<int> mgFixups = CreateFixupRange(numMaterialGroups, writer);

    for (int i = 0; i < numMaterialGroups; ++i)
    {
      writer.InsertFixup(mgFixups[i]);
      writer.Write(mesh->materialGroups[i]);
    }

    // write the data streams.
    // first # streams, then the actual streams
    writer.InsertFixup(streamFixup);
    int numStreams = (int)mesh->dataStreams.size();
    writer.Write(numStreams);
    vector<int> streamFixups = CreateFixupRange(numStreams, writer);

    for (int i = 0; i < numStreams; ++i)
    {
      writer.InsertFixup(streamFixups[i]);
      ImMesh::DataStream& d = mesh->dataStreams[i];

      writer.AddDeferredString(d.name);
      writer.Write(d.flags);
      writer.Write((int)d.data.size());
      writer.AddDeferredVector(d.data);
    }
  }

  //------------------------------------------------------------------------------
  void SaveCamera(const ImCamera* camera, const Options& options, DeferredWriter& writer)
  {
    SaveBase(camera, options, writer);

    writer.Write(camera->verticalFov);
    writer.Write(camera->nearPlane);
    writer.Write(camera->farPlane);
    writer.Write(camera->targetObj->id);
  }

  //------------------------------------------------------------------------------
  void SaveLight(const ImLight* light, const Options& options, DeferredWriter& writer)
  {
    SaveBase(light, options, writer);

    unordered_map<int, protocol::LightType> typeLookup = {
      { melange::LIGHT_TYPE_OMNI, protocol::LightType::Point },
      { melange::LIGHT_TYPE_DISTANT, protocol::LightType::Directional },
      { melange::LIGHT_TYPE_SPOT, protocol::LightType::Spot } };

    unordered_map<int, protocol::FalloffType> falloffLookup = {
      { melange::LIGHT_DETAILS_FALLOFF_NONE, protocol::FalloffType::None },
      { melange::LIGHT_DETAILS_FALLOFF_LINEAR, protocol::FalloffType::Linear } };

    writer.Write(typeLookup[light->type]);
    writer.Write(light->color);
    writer.Write(light->intensity);

    writer.Write(falloffLookup[light->falloffType]);
    writer.Write(light->falloffRadius);
    writer.Write(light->outerAngle);
  }

  //------------------------------------------------------------------------------
  void SaveSpline(const ImSpline* spline, const Options& options, DeferredWriter& writer)
  {
    SaveBase(spline, options, writer);

    writer.Write(spline->type);
    writer.Write((int)spline->points.size() / 3);
    writer.AddDeferredVector(spline->points);
    writer.Write(spline->isClosed);
  }

  //------------------------------------------------------------------------------
  void SaveNullObject(const ImNullObject* nullObject, const Options& options, DeferredWriter& writer)
  {
    SaveBase(nullObject, options, writer);
  }
}
