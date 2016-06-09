#include "melange_helpers.hpp"
#include "export_camera.hpp"
#include "export_mesh.hpp"
#include "export_light.hpp"
//#include "export_misc.hpp"
#include "im_exporter.hpp"

using namespace melange;

//-----------------------------------------------------------------------------
string CopyString(const melange::String& str)
{
  string res;
  if (char* c = str.GetCStringCopy())
  {
    res = string(c);
    melange::_MemFree((void**)&(c));
  }
  return res;
}

// overload this function and fill in your own unique data
void GetWriterInfo(Int32& id, String& appname)
{
  // register your own pluginid once for your exporter and enter it here under id
  // this id must be used for your own unique ids
  // 	Bool AddUniqueID(LONG appid, const CHAR *const mem, LONG bytes);
  // 	Bool FindUniqueID(LONG appid, const CHAR *&mem, LONG &bytes) const;
  // 	Bool GetUniqueIDIndex(LONG idx, LONG &id, const CHAR *&mem, LONG &bytes) const;
  // 	LONG GetUniqueIDCount() const;
  id = 0;
  appname = "C4d -> boba converter";
}

//-----------------------------------------------------------------------------
RootMaterial* AllocAlienRootMaterial()
{
  return NewObj(RootMaterial);
}

RootObject* AllocAlienRootObject()
{
  return NewObj(RootObject);
}

RootLayer* AllocAlienRootLayer()
{
  return NewObj(RootLayer);
}

RootRenderData* AllocAlienRootRenderData()
{
  return NewObj(RootRenderData);
}

RootViewPanel* AllocC4DRootViewPanel()
{
  return NewObj(RootViewPanel);
}

LayerObject* AllocAlienLayer()
{
  return NewObj(LayerObject);
}

//-----------------------------------------------------------------------------
NodeData* AllocAlienObjectData(Int32 id, Bool& known)
{
  NodeData* data = NULL;
  switch (id)
  {
    // supported element types
  case Opolygon: data = NewObj(AlienPolygonObjectData); break;
  case Ocamera: data = NewObj(AlienCameraObjectData); break;
  case Onull: data = NewObj(AlienNullObjectData); break;
  case Olight: data = NewObj(AlienLightObjectData); break;
  case Opoint: data = NewObj(AlienPointObjectData); break;
  case Osphere: data = NewObj(AlienPrimitiveObjectData); break;
  case Ocube: data = NewObj(AlienPrimitiveObjectData); break;
  case Oplane: data = NewObj(AlienPrimitiveObjectData); break;
  case Ocone: data = NewObj(AlienPrimitiveObjectData); break;
  case Otorus: data = NewObj(AlienPrimitiveObjectData); break;
  case Ocylinder: data = NewObj(AlienPrimitiveObjectData); break;
  }

  known = !!data;
  return data;
}

//-----------------------------------------------------------------------------
// allocate the plugin tag elements data (only few tags have additional data which is stored in a
// NodeData)
NodeData* AllocAlienTagData(Int32 id, Bool& known)
{
  return 0;
}

//-----------------------------------------------------------------------------
// create objects, material and layers for the new C4D scene file
Bool BaseDocument::CreateSceneToC4D(Bool selectedonly)
{
  return true;
}

//-----------------------------------------------------------------------------
namespace melange
{
  // memory allocation functions inside _melange_ namespace (if you have your own memory management
  // you can overload these functions)
  // alloc memory no clear
  void* MemAllocNC(Int size)
  {
    void* mem = malloc(size);
    return mem;
  }

  // alloc memory set to 0
  void* MemAlloc(Int size)
  {
    void* mem = MemAllocNC(size);
    memset(mem, 0, size);
    return mem;
  }

  // realloc existing memory
  void* MemRealloc(void* orimem, Int size)
  {
    void* mem = realloc(orimem, size);
    return mem;
  }

  // free memory and set pointer to null
  void MemFree(void*& mem)
  {
    if (!mem)
      return;

    free(mem);
    mem = NULL;
  }
}

// prints animation track and key infos to the console
static void PrintAnimInfo(BaseList2D* bl)
{
  if (!bl || !bl->GetFirstCTrack())
    return;

  printf("\n   # Animation Info #");

  Int32 tn = 0;
  CTrack* ct = bl->GetFirstCTrack();
  while (ct)
  {
    // CTrack name
    Char* pChar = ct->GetName().GetCStringCopy();
    if (pChar)
    {
      printf("\n   %d. CTrack \"%s\"!\n", (int)++tn, pChar);
      DeleteMem(pChar);
    }
    else
      printf("\n   %d. CTrack !\n", (int)++tn);

    // time track
    CTrack* tt = ct->GetTimeTrack(bl->GetDocument());
    if (tt)
    {
      printf("\n   -> has TimeTrack !\n");
      CCurve* tcc = ct->GetCurve();
      if (tcc)
      {
        printf("    Has CCurve with %d Keys\n", (int)tcc->GetKeyCount());

        CKey* ck = nullptr;
        BaseTime t;
        for (Int32 k = 0; k < tcc->GetKeyCount(); k++)
        {
          ck = tcc->GetKey(k);
          t = ck->GetTime();
          printf("     %d. Key : %d - %f\n", (int)k + 1, (int)t.GetFrame(25), ck->GetValue());
        }
      }
    }

    // get DescLevel id
    DescID testID = ct->GetDescriptionID();
    DescLevel lv = testID[0];
    ct->SetDescriptionID(ct, testID);
    printf("   DescID->DescLevel->ID: %d\n", (int)lv.id);

    // CTrack type
    switch (ct->GetTrackCategory())
    {
    case PSEUDO_VALUE: printf("   VALUE - Track found!\n"); break;

    case PSEUDO_DATA: printf("   DATA - Track found!\n"); break;

    case PSEUDO_PLUGIN:
      if (ct->GetType() == CTpla)
        printf("   PLA - Track found!\n");
      else if (ct->GetType() == CTdynamicspline)
        printf("   Dynamic Spline Data - Track found!\n");
      else if (ct->GetType() == CTmorph)
        printf("   MORPH - Track found!\n");
      else
        printf("   unknown PLUGIN - Track found!\n");
      break;

    case PSEUDO_UNDEF:
    default: printf("   UNDEFINDED - Track found!\n");
    }

    // get CCurve and print key frame data
    CCurve* cc = ct->GetCurve();
    if (cc)
    {
      printf("   Has CCurve with %d Keys\n", (int)cc->GetKeyCount());

      CKey* ck = nullptr;
      BaseTime t;
      for (Int32 k = 0; k < cc->GetKeyCount(); k++)
      {
        ck = cc->GetKey(k);

        t = ck->GetTime();
        if (ct->GetTrackCategory() == PSEUDO_VALUE)
        {
          printf("    %d. Key : %d - %f\n", (int)k + 1, (int)t.GetFrame(25), ck->GetValue());
        }
        else if (ct->GetTrackCategory() == PSEUDO_PLUGIN && ct->GetType() == CTpla)
        {
          GeData ptData;
          printf("    %d. Key : %d - ", (int)k + 1, (int)t.GetFrame(25));

          // bias
          if (ck->GetParameter(CK_PLA_BIAS, ptData) && ptData.GetType() == DA_REAL)
            printf("Bias = %.2f - ", ptData.GetFloat());

          // smooth
          if (ck->GetParameter(CK_PLA_CUBIC, ptData) && ptData.GetType() == DA_LONG)
            printf("Smooth = %d - ", (int)ptData.GetInt32());

          // pla data
          if (ck->GetParameter(CK_PLA_DATA, ptData))
          {
            PLAData* plaData = (PLAData*)ptData.GetCustomDataType(CUSTOMDATATYPE_PLA);
            PointTag* poiTag;
            TangentTag* tanTag;
            plaData->GetVariableTags(poiTag, tanTag);
            if (poiTag && poiTag->GetCount() > 0)
            {
              Vector* a = poiTag->GetPointAdr();
              // print values for first point only
              printf("%.3f / %.3f / %.3f", a[0].x, a[0].y, a[0].z);
            }
            else
              printf("no points?");
          }

          printf("\n");
        }
        else if (ct->GetTrackCategory() == PSEUDO_PLUGIN && ct->GetType() == CTmorph)
        {
          GeData mtData;
          printf("    %d. Key : %d - ", (int)k + 1, (int)t.GetFrame(25));

          // bias
          if (ck->GetParameter(CK_MORPH_BIAS, mtData) && mtData.GetType() == DA_REAL)
            printf("Bias = %.2f - ", mtData.GetFloat());

          // smooth
          if (ck->GetParameter(CK_MORPH_CUBIC, mtData) && mtData.GetType() == DA_LONG)
            printf("Smooth = %d - ", (int)mtData.GetInt32());

          // link to target object
          if (ck->GetParameter(CK_MORPH_LINK, mtData))
          {
            BaseObject* targetObject = (BaseObject*)mtData.GetLink();
            if (targetObject)
            {
              Char* pTargetChar = targetObject->GetName().GetCStringCopy();
              if (pTargetChar)
              {
                printf("Target Object = %s", pTargetChar);
                DeleteMem(pTargetChar);
              }
              else
                printf("no target object name");
            }
            else
              printf("no target object defined...");
          }

          printf("\n");
        }
      } // for
    }
    ct = ct->GetNext();
  }
  printf("\n");
}
