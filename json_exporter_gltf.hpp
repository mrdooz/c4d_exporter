#pragma once
#include "im_scene.hpp"
#include "exporter.hpp"

bool ExportAsJsonGltf(const ImScene& scene, const Options& options, SceneStats* stats);
