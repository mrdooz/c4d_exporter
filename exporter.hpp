#pragma once
#include "im_scene.hpp"

#define WITH_XFORM_MTX 0

static u32 DEFAULT_MATERIAL = ~0u;

class DeferredWriter;

namespace melange
{
  class AlienMaterial;
}

struct Options
{
  string inputFilename;
  string outputFilename;
  // c:/tjong/bla.1, base = 'bla', prefix = 'c:/tjong/bla'
  string outputBase;
  string outputPrefix;
  FILE* logfile = nullptr;
  bool optimizeIndices = false;
  bool compressVertices = false;
  bool compressIndices = false;
  int loglevel = 1;
};

//------------------------------------------------------------------------------
struct SceneStats
{
  int nullObjectSize = 0;
  int cameraSize = 0;
  int meshSize = 0;
  int lightSize = 0;
  int materialSize = 0;
  int splineSize = 0;
  int animationSize = 0;
  int dataSize = 0;
};

extern ImScene g_scene;
extern Options options;
extern vector<function<bool()>> g_deferredFunctions;

namespace melange
{
  class AlienBaseDocument;
  class HyperFile;
}

extern melange::AlienBaseDocument* g_Doc;
extern melange::HyperFile* g_File;

