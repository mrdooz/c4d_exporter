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
  string outputDirectory;
  // c:/tjong/bla.1, base = 'bla', prefix = 'c:/tjong/bla'
  string outputBase;
  string outputPrefix;
  FILE* logfile = nullptr;
  bool optimizeIndices = false;
  bool compressVertices = false;
  bool compressIndices = false;
  int loglevel = 1;
  bool force = false;
  bool sdf = false;
  int gridSize = 32;
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

namespace melange
{
  class AlienBaseDocument;
  class HyperFile;
}

struct ExportInstance
{
  ~ExportInstance();
  void Reset();
  void Log(int level, const char* fmt, ...) const;
  ImScene* scene = nullptr;
  Options options;
  // Fixup functions called after the scene has been read and processed.
  vector<function<bool()>> deferredFunctions;
  melange::AlienBaseDocument* doc = nullptr;
  melange::HyperFile* file = nullptr;
};

extern ExportInstance g_ExportInstance;

