#pragma once

namespace exporter
{
  bool SaveScene(const ImScene& scene, const Options& options, SceneStats* stats);
  void SaveMaterial(const ImMaterial* material, const Options& options, DeferredWriter& writer);
  void SaveMesh(ImMesh* mesh, const Options& options, DeferredWriter& writer);
  void SaveCamera(const ImCamera* camera, const Options& options, DeferredWriter& writer);
  void SaveLight(const ImLight* light, const Options& options, DeferredWriter& writer);
  void SaveNullObject(const ImNullObject* nullObject, const Options& options, DeferredWriter& writer);
  void SaveSpline(const ImSpline* spline, const Options& options, DeferredWriter& writer);
}
