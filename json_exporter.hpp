#pragma once
#include "im_scene.hpp"
#include "exporter.hpp"

struct JsonWriter;

struct JsonExporter
{
  JsonExporter(const ExportInstance& instance) : instance(instance)
  {
  }

  bool Export(SceneStats* stats);

  void ExportSceneInfo(JsonWriter* w);
  void ExportBase(ImBaseObject* obj, JsonWriter* w);
  void ExportNullObjects(const vector<ImNullObject*>& nullObjects, JsonWriter* w);
  void ExportCameras(const vector<ImCamera*>& cameras, JsonWriter* w);
  void ExportLights(const vector<ImLight*>& lights, JsonWriter* w);
  void ExportWorldGeometry(JsonWriter* w);
  void ExportMeshData(ImMesh* mesh, JsonWriter* w);
  void ExportAnimationTracks(ImBaseObject* obj, JsonWriter* w);
  void ExportMeshes(const vector<ImMesh*>& meshes, JsonWriter* w);
  void ExportPrimitives(const vector<ImPrimitive*>& primitives, JsonWriter* w);
  void ExportMaterials(const vector<ImMaterial*>& materials, JsonWriter* w);
  void ExportMaterialComponentShader(const ImMaterialComponent& component, JsonWriter* w);

  void CreateSDF3(const ExportInstance& instance, JsonWriter* w);

  void AddToBuffer(const char* data, size_t len, const string& name, JsonWriter* w);

  template <typename T>
  void AddToBuffer(const vector<T>& v, const string& name, JsonWriter* w)
  {
    AddToBuffer((const char*)v.data(), v.size() * sizeof(T), name, w);
  }

  ExportInstance instance;
};

