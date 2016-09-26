//-----------------------------------------------------------------------------
// Cinema4 melange exporter
// magnus.osterlind@gmail.com
//-----------------------------------------------------------------------------

#include "exporter.hpp"
#include "arg_parse.hpp"
#include "exporter_utils.hpp"
#include "json_exporter.hpp"
#include "melange_helpers.hpp"

//-----------------------------------------------------------------------------
namespace
{
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
    return inputFilename.substr(startPos, lenToDot - startPos) + ".json";
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
melange::BaseObject* ImScene::FindMelangeObject(ImBaseObject* obj)
{
  auto it = imObjectToMelange.find(obj);
  return it == imObjectToMelange.end() ? nullptr : it->second;
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
static void CollectAnimationTracks()
{
  melange::GeData mydata;

  // get fps and start/end time
  if (g_Doc->GetParameter(melange::DOCUMENT_FPS, mydata))
    g_scene.fps = mydata.GetInt32();

  if (g_Doc->GetParameter(melange::DOCUMENT_MINTIME, mydata))
    g_scene.startTime = mydata.GetTime().Get();

  if (g_Doc->GetParameter(melange::DOCUMENT_MAXTIME, mydata))
    g_scene.endTime = mydata.GetTime().Get();

  for (melange::BaseObject* obj = g_Doc->GetFirstObject(); obj; obj = obj->GetNext())
  {
    for (melange::CTrack* track = obj->GetFirstCTrack(); track; track = track->GetNext())
    {
      ImBaseObject* imObj = g_scene.FindObject(obj);
      if (!imObj)
      {
        LOG(1, "Unable to find animated ImObject: %s\n", CopyString(obj->GetName()).c_str());
        break;
      }

      ImSampledTrack imTrack;
      imTrack.name = CopyString(track->GetName());
      imTrack.name = ReplaceAll(imTrack.name, ' ', 0);

      // sample the track
      float inc = (g_scene.endTime - g_scene.startTime) / g_scene.fps;
      int startFrame = g_scene.startTime * g_scene.fps;
      int endFrame = g_scene.endTime * g_scene.fps;

      float curTime = g_scene.startTime;
      imTrack.values.resize(endFrame - startFrame + 1);
      for (int curFrame = startFrame; curFrame <= endFrame; curFrame++)
      {
        float value = track->GetValue(g_Doc, melange::BaseTime((float)curFrame / g_scene.fps), g_scene.fps);
        imTrack.values[curFrame - startFrame] = value;
        curTime += inc;
      }

      imObj->sampledAnimTracks.push_back(imTrack);
    }
  }
}

//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
  ArgParse parser;
  parser.AddFlag(nullptr, "compress-vertices", &options.compressVertices);
  parser.AddFlag(nullptr, "compress-indices", &options.compressIndices);
  parser.AddFlag(nullptr, "optimize-indices", &options.optimizeIndices);
  parser.AddFlag("f", "force", &options.force);
  parser.AddFlag(nullptr, "sdf", &options.sdf);
  parser.AddIntArgument(nullptr, "loglevel", &options.loglevel);
  parser.AddStringArgument("o", nullptr, &options.outputDirectory);
  parser.AddIntArgument(nullptr, "grid-size", &options.gridSize);

  if (!parser.Parse(argc - 1, argv + 1))
  {
    fprintf(stderr, "%s", parser.error.c_str());
    return 1;
  }

  // the positional argument is a filename glob
  if (parser.positional.empty())
  {
    printf("No filename given.\n");
    return 1;
  }

  WIN32_FIND_DATAA findData;
  const char* glob = parser.positional.front().c_str();
  HANDLE h = FindFirstFileA(glob, &findData);
  if (h == INVALID_HANDLE_VALUE)
  {
    printf("Invalid glob: %s\n", glob);
    return 1;
  }

  while (true)
  {
    const char* filename = &findData.cFileName[0];
    string outputFilename = options.outputDirectory + string("/") + FilenameFromInput(filename, false);

    // normalize input directory
    for (char* ptr = (char*)glob; *ptr; ++ptr)
    {
      if (*ptr == '\\')
        *ptr = '/';
    }

    // get input dir
    const char* lastInputSlash = strrchr(glob, '/');
    string inputDir(glob, lastInputSlash - glob + 1);
    options.inputFilename = inputDir + filename;

    // normalize output filename, and capture the base
    size_t lastSlash = -1;
    size_t lastDot = -1;
    for (size_t i = 0; i < outputFilename.size(); ++i)
    {
      if (outputFilename[i] == '\\')
        outputFilename[i] = '/';

      if (outputFilename[i] == '/')
        lastSlash = i;
      else if (outputFilename[i] == '.')
        lastDot = i;
    }

    if (lastSlash != -1 && lastDot != -1)
    {
      options.outputBase = string(outputFilename.data() + lastSlash + 1, lastDot - lastSlash - 1);
      options.outputPrefix = string(outputFilename.data(), lastDot);
    }

    // skip the file if the output file is older than the input
    bool processFile = true;

    // skip checking timestamps is the force flag is given
    if (!options.force)
    {
      struct stat statInput;
      struct stat statOutput;
      if (stat(options.inputFilename.c_str(), &statInput) == 0
          && stat(outputFilename.c_str(), &statOutput) == 0)
      {
        processFile = statInput.st_mtime > statOutput.st_mtime;
      }
    }

    if (processFile)
    {
      options.logfile = fopen((outputFilename + ".log").c_str(), "at");

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
          outputFilename.c_str());

      g_Doc = NewObj(melange::AlienBaseDocument);
      g_File = NewObj(melange::HyperFile);

      if (!g_File->Open(DOC_IDENT, options.inputFilename.c_str(), melange::FILEOPEN_READ))
        return 1;

      if (!g_Doc->ReadObject(g_File, true))
        return 1;

      g_File->Close();

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

      CollectAnimationTracks();

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
    }

    if (!FindNextFileA(h, &findData))
      break;
  }

  FindClose(h);

  if (IsDebuggerPresent())
  {
    printf("==] esc to quit [==\n");
    while (!GetAsyncKeyState(VK_ESCAPE))
      continue;
  }

  return 0;
}
