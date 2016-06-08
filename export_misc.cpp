#include "export_misc.hpp"
#include "exporter_utils.hpp"
#include "melange_helpers.hpp"

//-----------------------------------------------------------------------------
bool melange::AlienPrimitiveObjectData::Execute()
{
  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();

  string name(CopyString(baseObj->GetName()));
  LOG(1, "Skipping primitive object: %s\n", name.c_str());
  return true;
}

//-----------------------------------------------------------------------------
void ExportSplineChildren(melange::BaseObject* baseObj)
{
  // Export spline objects that are children to the null object
  vector<melange::BaseObject*> children;
  GetChildren(baseObj, &children);
  for (melange::BaseObject* obj : children)
  {
    int type = obj->GetType();
    switch (type)
    {
      case Ospline:
      {
        ExportSpline(obj);
        break;
      }
    }
  }
}

//-----------------------------------------------------------------------------
bool melange::AlienNullObjectData::Execute()
{
  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();
  const string name = CopyString(baseObj->GetName());

#if WITH_SCENE_1
  exporter::NullObject* nullObject = new exporter::NullObject(baseObj);
#if WITH_XFORM_MTX
  CopyMatrix(baseObj->GetMl(), nullObject->mtxLocal);
  CopyMatrix(baseObj->GetMg(), nullObject->mtxGlobal);
#endif

  CopyTransform(baseObj->GetMl(), &nullObject->xformLocal);
  CopyTransform(baseObj->GetMg(), &nullObject->xformGlobal);

  g_scene.nullObjects.push_back(nullObject);

  ExportSplineChildren(baseObj);
#endif

  //{
  //  shared_ptr<scene::NullObject> nullObject = make_shared<scene::NullObject>();
  //  nullObject->name = name;
  //  nullObject->id = g_ObjectId.NextId();

  //  CopyTransform(baseObj->GetMl(), &nullObject->xformLocal);
  //  CopyTransform(baseObj->GetMg(), &nullObject->xformGlobal);

  //  g_Scene2.nullObjects.push_back(nullObject);
  //}

  return true;
}

//-----------------------------------------------------------------------------
void CollectionAnimationTracksForObj(melange::BaseList2D* bl, vector<exporter::Track>* tracks)
{
  if (!bl || !bl->GetFirstCTrack())
    return;

  for (melange::CTrack* ct = bl->GetFirstCTrack(); ct; ct = ct->GetNext())
  {
    // CTrack name
    exporter::Track track;
    track.name = CopyString(ct->GetName());

    // time track
    melange::CTrack* tt = ct->GetTimeTrack(bl->GetDocument());
    if (tt)
    {
      LOG(1, "Time track is unsupported");
    }

    // get DescLevel id
    melange::DescID testID = ct->GetDescriptionID();
    melange::DescLevel lv = testID[0];
    ct->SetDescriptionID(ct, testID);

    // get CCurve and print key frame data
    melange::CCurve* cc = ct->GetCurve();
    if (cc)
    {
      exporter::Curve curve;
      curve.name = CopyString(cc->GetName());

      for (int k = 0; k < cc->GetKeyCount(); k++)
      {
        melange::CKey* ck = cc->GetKey(k);
        melange::BaseTime t = ck->GetTime();
        if (ct->GetTrackCategory() == melange::PSEUDO_VALUE)
        {
          curve.keyframes.push_back(
              exporter::Keyframe{(int)t.GetFrame(g_Doc->GetFps()), (float)ck->GetValue()});
        }
        else if (ct->GetTrackCategory() == melange::PSEUDO_PLUGIN && ct->GetType() == CTpla)
        {
          LOG(1, "Plugin keyframes are unsupported");
        }
        else if (ct->GetTrackCategory() == melange::PSEUDO_PLUGIN && ct->GetType() == CTmorph)
        {
          LOG(1, "Morph keyframes are unsupported");
        }
      }

      track.curves.push_back(curve);
    }
    tracks->push_back(track);
  }
}

//-----------------------------------------------------------------------------
void CollectMaterials(melange::AlienBaseDocument* c4dDoc)
{
  // add default material
  g_scene.materials.push_back(new exporter::Material());
  exporter::Material* exporterMaterial = g_scene.materials.back();
  exporterMaterial->mat = nullptr;
  exporterMaterial->name = "<default>";
  exporterMaterial->id = ~0u;

  exporterMaterial->components.push_back(
      exporter::MaterialComponent{"color", exporter::Color(0.5f, 0.5f, 0.5f), "", 1});

  for (melange::BaseMaterial* mat = c4dDoc->GetFirstMaterial(); mat; mat = mat->GetNext())
  {
    // check if the material is a standard material
    if (mat->GetType() != Mmaterial)
      continue;

    string name = CopyString(mat->GetName());

    g_scene.materials.push_back(new exporter::Material());
    exporter::Material* exporterMaterial = g_scene.materials.back();
    exporterMaterial->mat = mat;
    exporterMaterial->name = name;

    // check if the given channel is used in the material
    if (((melange::Material*)mat)->GetChannelState(CHANNEL_COLOR))
    {
      exporterMaterial->components.push_back(exporter::MaterialComponent{"color",
          GetVectorParam<exporter::Color>(mat, melange::MATERIAL_COLOR_COLOR),
          "",
          GetFloatParam(mat, melange::MATERIAL_COLOR_BRIGHTNESS)});
    }

    if (((melange::Material*)mat)->GetChannelState(CHANNEL_REFLECTION))
    {
      exporterMaterial->components.push_back(exporter::MaterialComponent{"refl",
          GetVectorParam<exporter::Color>(mat, melange::MATERIAL_REFLECTION_COLOR),
          "",
          GetFloatParam(mat, melange::MATERIAL_REFLECTION_BRIGHTNESS)});
    }

    if (((melange::Material*)mat)->GetChannelState(CHANNEL_LUMINANCE))
    {
      exporterMaterial->components.push_back(exporter::MaterialComponent{"lumi",
          GetVectorParam<exporter::Color>(mat, melange::MATERIAL_LUMINANCE_COLOR),
          "",
          GetFloatParam(mat, melange::MATERIAL_LUMINANCE_BRIGHTNESS)});
    }
  }
}

//-----------------------------------------------------------------------------
void CollectMaterials2(melange::AlienBaseDocument* c4dDoc)
{
  // add default material
#if 0
  shared_ptr<scene::Material> defaultMaterial = make_shared<scene::Material>();
  defaultMaterial->name = "<default>";
  defaultMaterial->id = ~0;
  defaultMaterial->components.push_back(make_shared<scene::Material::Component>(
      scene::Material::Component{'COLR', 1, 1, 1, 1, "", 1}));
  g_Scene2.materials.push_back(defaultMaterial);

  for (melange::BaseMaterial* baseMat = c4dDoc->GetFirstMaterial(); baseMat;
       baseMat = baseMat->GetNext())
  {
    // check if the material is a standard material
    if (baseMat->GetType() != Mmaterial)
      continue;

    scene::Material* sceneMat = new scene::Material();
    string name = CopyString(baseMat->GetName());
    sceneMat->name = CopyString(baseMat->GetName());
    sceneMat->id = g_MaterialId.NextId();
    g_MaterialIdToObj[sceneMat->id] = baseMat;

    melange::Material* mat = (melange::Material*)baseMat;

    // check if the given channel is used in the material
    if (mat->GetChannelState(CHANNEL_COLOR))
    {
      exporter::Color col = GetVectorParam<exporter::Color>(baseMat, melange::MATERIAL_COLOR_COLOR);
      sceneMat->components.push_back(
          make_shared<scene::Material::Component>(scene::Material::Component{'COLR',
              col.r,
              col.g,
              col.b,
              col.a,
              "",
              GetFloatParam(baseMat, melange::MATERIAL_COLOR_BRIGHTNESS)}));
    }

    if (mat->GetChannelState(CHANNEL_REFLECTION))
    {
      exporter::Color col =
          GetVectorParam<exporter::Color>(baseMat, melange::MATERIAL_REFLECTION_COLOR);
      sceneMat->components.push_back(
          make_shared<scene::Material::Component>(scene::Material::Component{'REFL',
              col.r,
              col.g,
              col.b,
              col.a,
              "",
              GetFloatParam(baseMat, melange::MATERIAL_REFLECTION_BRIGHTNESS)}));
    }
  }
#endif
}

//-----------------------------------------------------------------------------
exporter::BaseObject::BaseObject(melange::BaseObject* melangeObj)
    : melangeObj(melangeObj)
    , parent(g_scene.FindObject(melangeObj->GetUp()))
    , name(CopyString(melangeObj->GetName()))
    , id(Scene::nextObjectId++)
{
  LOG(1, "Exporting: %s\n", name.c_str());
  melange::BaseObject* melangeParent = melangeObj->GetUp();
  if ((melangeParent != nullptr) ^ (parent != nullptr))
  {
    LOG(1,
        "  Unable to find parent! (%s)\n",
        melangeParent ? CopyString(melangeParent->GetName()).c_str() : "");
    valid = false;
  }

  g_scene.objMap[melangeObj] = this;
}

//-----------------------------------------------------------------------------
void ExportSpline(melange::BaseObject* obj)
{
  melange::SplineObject* splineObject = static_cast<melange::SplineObject*>(obj);
  string splineName = CopyString(splineObject->GetName());

  melange::SPLINETYPE splineType = splineObject->GetSplineType();
  bool isClosed = splineObject->GetIsClosed();
  int pointCount = splineObject->GetPointCount();
  const melange::Vector* points = splineObject->GetPointR();

  exporter::Spline* s = new exporter::Spline(splineObject);
  s->type = splineType;
  s->isClosed = isClosed;

  s->points.reserve(pointCount * 3);
  for (int i = 0; i < pointCount; ++i)
  {
    s->points.push_back(points[i].x);
    s->points.push_back(points[i].y);
    s->points.push_back(points[i].z);
  }

  g_scene.splines.push_back(s);
}
