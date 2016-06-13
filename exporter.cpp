//-----------------------------------------------------------------------------
// Cinema4 melange exporter
// magnus.osterlind@gmail.com
//-----------------------------------------------------------------------------

#include "exporter.hpp"
#include "melange_helpers.hpp"
#include "arg_parse.hpp"
#include "exporter_utils.hpp"
#include "json_exporter.hpp"

//-----------------------------------------------------------------------------
namespace
{
  string ReplaceAll(const string& str, char toReplace, char replaceWith)
  {
    string res(str);
    for (size_t i = 0; i < res.size(); ++i)
    {
      if (res[i] == toReplace)
        res[i] = replaceWith;
    }
    return res;
  }

  //------------------------------------------------------------------------------
  string MakeCanonical(const string& str)
  {
    // convert back slashes to forward
    return ReplaceAll(str, '\\', '/');
  }
}

melange::AlienBaseDocument* g_Doc;
melange::HyperFile* g_File;

ImScene g_scene;
Options options;

// Fixup functions called after the scene has been read and processed.
vector<function<bool()>> g_deferredFunctions;

u32 ImScene::nextObjectId = 1;
u32 ImMaterial::nextId;

unordered_map<melange::BaseObject*, vector<ImTrack>> g_AnimationTracks;

//-----------------------------------------------------------------------------
string FilenameFromInput(const string& inputFilename, bool stripPath)
{
  const char* ff = inputFilename.c_str();
  const char* dot = strrchr(ff, '.');
  if (dot)
  {
    int lenToDot = dot - ff;
    int startPos = 0;

    if (stripPath)
    {
      const char* slash = strrchr(ff, '/');
      startPos = slash ? slash - ff + 1 : 0;
    }
    return inputFilename.substr(startPos, lenToDot - startPos) + ".boba";
  }

  printf("Invalid input filename given: %s\n", inputFilename.c_str());
  return string();
}

//------------------------------------------------------------------------------
ImBaseObject* ImScene::FindObject(melange::BaseObject* obj)
{
  auto it = melangeToImObject.find(obj);
  return it == melangeToImObject.end() ? nullptr : it->second;
}

//-----------------------------------------------------------------------------
ImMaterial* ImScene::FindMaterial(melange::BaseMaterial* mat)
{
  for (ImMaterial* m : materials)
  {
    if (m->mat == mat)
      return m;
  }

  return nullptr;
}

//-----------------------------------------------------------------------------
void ExportAnimations()
{
  melange::GeData mydata;
  float startTime = 0.0, endTime = 0.0;
  int startFrame = 0, endFrame = 0, fps = 0;

  // get fps
  if (g_Doc->GetParameter(melange::DOCUMENT_FPS, mydata))
    fps = mydata.GetInt32();

  // get start and end time
  if (g_Doc->GetParameter(melange::DOCUMENT_MINTIME, mydata))
    startTime = mydata.GetTime().Get();

  if (g_Doc->GetParameter(melange::DOCUMENT_MAXTIME, mydata))
    endTime = mydata.GetTime().Get();

  // calculate start and end frame
  startFrame = startTime * fps;
  endFrame = endTime * fps;

  float inc = 1.0f / fps;
  float curTime = startTime;

  while (curTime <= endTime)
  {
    g_Doc->SetTime(melange::BaseTime(curTime));
    g_Doc->Execute();

    curTime += inc;
  }
}

//-----------------------------------------------------------------------------
bool ParseFilenames(const vector<string>& args)
{
  int curArg = 0;
  int remaining = (int)args.size();

  const auto& step = [&curArg, &remaining](int steps) {
    curArg += steps;
    remaining -= steps;
  };

  if (remaining < 1)
  {
    printf("Invalid args\n");
    return false;
  }

  options.inputFilename = MakeCanonical(args[0]);
  step(1);

  // create output file
  if (remaining == 1)
  {
    // check if the remaining argument is a file name, or just a directory
    if (strstr(args[curArg].c_str(), "boba") != nullptr)
    {
      options.outputFilename = args[curArg];
    }
    else
    {
      // a directory was given, so create the filename from the input file
      string res = FilenameFromInput(options.inputFilename, true);
      if (res.empty())
        return false;

      options.outputFilename = string(args[curArg]) + '/' + res;
    }
  }
  else
  {
    options.outputFilename = FilenameFromInput(options.inputFilename, false);
    if (options.outputFilename.empty())
      return false;
  }

  options.logfile = fopen((options.outputFilename + ".log").c_str(), "at");

  // normalize output filename, and capture the base
  string& s = options.outputFilename;
  size_t lastSlash = -1;
  size_t lastDot = -1;
  for (size_t i = 0; i < s.size(); ++i)
  {
    if (s[i] == '\\')
      s[i] = '/';

    if (s[i] == '/')
      lastSlash = i;
    else if (s[i] == '.')
      lastDot = i;
  }

  if (lastSlash != -1 && lastDot != -1)
  {
    options.outputBase = string(s.data() + lastSlash + 1, lastDot - lastSlash - 1);
    options.outputPrefix = string(s.data(), lastDot);
  }
  return true;
}

//-----------------------------------------------------------------------------
void CollectAnimationTracks()
{
  for (melange::BaseObject* obj = g_Doc->GetFirstObject(); obj; obj = obj->GetNext())
  {
    CollectionAnimationTracksForObj(obj, &g_AnimationTracks[obj]);
  }
}

//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
  ArgParse parser;
  parser.AddFlag(nullptr, "compress-vertices", &options.compressVertices);
  parser.AddFlag(nullptr, "compress-indices", &options.compressIndices);
  parser.AddFlag(nullptr, "optimize-indices", &options.optimizeIndices);
  parser.AddIntArgument(nullptr, "loglevel", &options.loglevel);

  if (!parser.Parse(argc - 1, argv + 1))
  {
    fprintf(stderr, "%s", parser.error.c_str());
    return 1;
  }

  if (!ParseFilenames(parser.positional))
  {
    fprintf(stderr, "Error parsing filenames");
    return 1;
  }

  time_t startTime = time(0);
  struct tm* now = localtime(&startTime);

  LOG(1,
      "==] STARTING [=================================] %.4d:%.2d:%.2d-%.2d:%.2d:%.2d ]==\n%s -> "
      "%s\n",
      now->tm_year + 1900,
      now->tm_mon + 1,
      now->tm_mday,
      now->tm_hour,
      now->tm_min,
      now->tm_sec,
      options.inputFilename.c_str(),
      options.outputFilename.c_str());

  g_Doc = NewObj(melange::AlienBaseDocument);
  g_File = NewObj(melange::HyperFile);

  if (!g_File->Open(DOC_IDENT, options.inputFilename.c_str(), melange::FILEOPEN_READ))
    return 1;

  if (!g_Doc->ReadObject(g_File, true))
    return 1;

  g_File->Close();

  CollectAnimationTracks();
  CollectMaterials(g_Doc);
  CollectMaterials2(g_Doc);
  g_Doc->CreateSceneFromC4D();

  bool res = true;
  for (auto& fn : g_deferredFunctions)
  {
    res &= fn();
    if (!res)
      break;
  }

  ExportAnimations();

  SceneStats stats;
  if (res)
  {
    ExportAsJson(g_scene, options, &stats);
  }

  DeleteObj(g_Doc);
  DeleteObj(g_File);

  LOG(2,
      "--> stats: \n"
      "    null object size: %.2f kb\n"
      "    camera object size: %.2f kb\n"
      "    mesh object size: %.2f kb\n"
      "    light object size: %.2f kb\n"
      "    material object size: %.2f kb\n"
      "    spline object size: %.2f kb\n"
      "    animation object size: %.2f kb\n"
      "    data object size: %.2f kb\n",
      (float)stats.nullObjectSize / 1024,
      (float)stats.cameraSize / 1024,
      (float)stats.meshSize / 1024,
      (float)stats.lightSize / 1024,
      (float)stats.materialSize / 1024,
      (float)stats.splineSize / 1024,
      (float)stats.animationSize / 1024,
      (float)stats.dataSize / 1024);

  time_t endTime = time(0);
  now = localtime(&endTime);

  LOG(1,
      "==] DONE [=====================================] %.4d:%.2d:%.2d-%.2d:%.2d:%.2d ]==\n",
      now->tm_year + 1900,
      now->tm_mon + 1,
      now->tm_mday,
      now->tm_hour,
      now->tm_min,
      now->tm_sec);

  if (options.logfile)
    fclose(options.logfile);

  if (IsDebuggerPresent())
  {
    while (!GetAsyncKeyState(VK_ESCAPE))
      continue;
  }

  return 0;
}
