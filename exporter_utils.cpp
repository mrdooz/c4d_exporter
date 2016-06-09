#include "exporter_utils.hpp"
#include "exporter.hpp"
#include "melange_helpers.hpp"

IdGenerator g_ObjectId(0);
IdGenerator g_MaterialId(1);
unordered_map<int, melange::BaseMaterial*> g_MaterialIdToObj;

//-----------------------------------------------------------------------------
void CopyTransform(const melange::Matrix& mtx, ImTransform* xform)
{
  xform->pos = mtx.off;
  xform->rot = melange::MatrixToHPB(mtx, melange::ROTATIONORDER_HPB);
  xform->scale = melange::Vector(Len(mtx.v1), Len(mtx.v2), Len(mtx.v3));
}

//-----------------------------------------------------------------------------
template <typename T>
void CopyVectorToArray(const T& v, float* out)
{
  out[0] = v.x;
  out[1] = v.y;
  out[2] = v.z;
}

//-----------------------------------------------------------------------------
void CopyMatrix(const melange::Matrix& mtx, float* out)
{
  out[0] = (float)mtx.v1.x;
  out[1] = (float)mtx.v1.y;
  out[2] = (float)mtx.v1.z;
  out[3] = (float)mtx.v2.x;
  out[4] = (float)mtx.v2.y;
  out[5] = (float)mtx.v2.z;
  out[6] = (float)mtx.v3.x;
  out[7] = (float)mtx.v3.y;
  out[8] = (float)mtx.v3.z;
  out[9] = (float)mtx.off.x;
  out[10] = (float)mtx.off.y;
  out[11] = (float)mtx.off.z;
}

//-----------------------------------------------------------------------------
void GetChildren(melange::BaseObject* obj, vector<melange::BaseObject*>* children)
{
  melange::BaseObject* child = obj->GetDown();
  while (child)
  {
    children->push_back(child);
    child = child->GetNext();
  }
}

//-----------------------------------------------------------------------------
void CollectionAnimationTracksForObj(melange::BaseList2D* bl, vector<ImTrack>* tracks)
{
  if (!bl || !bl->GetFirstCTrack())
    return;

  for (melange::CTrack* ct = bl->GetFirstCTrack(); ct; ct = ct->GetNext())
  {
    ImTrack track;
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
      ImCurve curve;
      curve.name = CopyString(cc->GetName());

      for (int k = 0; k < cc->GetKeyCount(); k++)
      {
        melange::CKey* ck = cc->GetKey(k);
        melange::BaseTime t = ck->GetTime();
        if (ct->GetTrackCategory() == melange::PSEUDO_VALUE)
        {
          curve.keyframes.push_back(
            ImKeyframe{ (int)t.GetFrame(g_Doc->GetFps()), (float)ck->GetValue() });
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
  g_scene.materials.push_back(new ImMaterial());
  ImMaterial* exporterMaterial = g_scene.materials.back();
  exporterMaterial->mat = nullptr;
  exporterMaterial->name = "<default>";
  exporterMaterial->id = ~0u;

  exporterMaterial->components.push_back(
    ImMaterialComponent{ "color", Color(0.5f, 0.5f, 0.5f), "", 1 });

  for (melange::BaseMaterial* mat = c4dDoc->GetFirstMaterial(); mat; mat = mat->GetNext())
  {
    // check if the material is a standard material
    if (mat->GetType() != Mmaterial)
      continue;

    string name = CopyString(mat->GetName());

    g_scene.materials.push_back(new ImMaterial());
    ImMaterial* exporterMaterial = g_scene.materials.back();
    exporterMaterial->mat = mat;
    exporterMaterial->name = name;

    // check if the given channel is used in the material
    if (((melange::Material*)mat)->GetChannelState(CHANNEL_COLOR))
    {
      exporterMaterial->components.push_back(ImMaterialComponent{ "color",
        GetVectorParam<Color>(mat, melange::MATERIAL_COLOR_COLOR),
        "",
        GetFloatParam(mat, melange::MATERIAL_COLOR_BRIGHTNESS) });
    }

    if (((melange::Material*)mat)->GetChannelState(CHANNEL_REFLECTION))
    {
      exporterMaterial->components.push_back(ImMaterialComponent{ "refl",
        GetVectorParam<Color>(mat, melange::MATERIAL_REFLECTION_COLOR),
        "",
        GetFloatParam(mat, melange::MATERIAL_REFLECTION_BRIGHTNESS) });
    }

    if (((melange::Material*)mat)->GetChannelState(CHANNEL_LUMINANCE))
    {
      exporterMaterial->components.push_back(ImMaterialComponent{ "lumi",
        GetVectorParam<Color>(mat, melange::MATERIAL_LUMINANCE_COLOR),
        "",
        GetFloatParam(mat, melange::MATERIAL_LUMINANCE_BRIGHTNESS) });
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
    scene::Material::Component{ 'COLR', 1, 1, 1, 1, "", 1 }));
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
        make_shared<scene::Material::Component>(scene::Material::Component{ 'COLR',
        col.r,
        col.g,
        col.b,
        col.a,
        "",
        GetFloatParam(baseMat, melange::MATERIAL_COLOR_BRIGHTNESS) }));
    }

    if (mat->GetChannelState(CHANNEL_REFLECTION))
    {
      exporter::Color col =
        GetVectorParam<exporter::Color>(baseMat, melange::MATERIAL_REFLECTION_COLOR);
      sceneMat->components.push_back(
        make_shared<scene::Material::Component>(scene::Material::Component{ 'REFL',
        col.r,
        col.g,
        col.b,
        col.a,
        "",
        GetFloatParam(baseMat, melange::MATERIAL_REFLECTION_BRIGHTNESS) }));
    }
  }
#endif
}
