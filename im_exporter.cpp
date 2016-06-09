#include "im_exporter.hpp"
#include "exporter_utils.hpp"
#include "melange_helpers.hpp"
#include "exporter.hpp"

using namespace melange;

//-----------------------------------------------------------------------------
static const float DEFAULT_NEAR_PLANE = 1.0f;
static const float DEFAULT_FAR_PLANE = 1000.0f;

//-----------------------------------------------------------------------------
static void ExportSpline(melange::BaseObject* obj)
{
  melange::SplineObject* splineObject = static_cast<melange::SplineObject*>(obj);
  string splineName = CopyString(splineObject->GetName());

  melange::SPLINETYPE splineType = splineObject->GetSplineType();
  bool isClosed = splineObject->GetIsClosed();
  int pointCount = splineObject->GetPointCount();
  const melange::Vector* points = splineObject->GetPointR();

  ImSpline* s = new ImSpline(splineObject);
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

//-----------------------------------------------------------------------------
static void ExportSplineChildren(melange::BaseObject* baseObj)
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
bool melange::AlienPrimitiveObjectData::Execute()
{
  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();

  string name(CopyString(baseObj->GetName()));
  LOG(1, "Skipping primitive object: %s\n", name.c_str());
  return true;
}

//-----------------------------------------------------------------------------
bool melange::AlienNullObjectData::Execute()
{
  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();
  const string name = CopyString(baseObj->GetName());

  ImNullObject* nullObject = new ImNullObject(baseObj);

  CopyTransform(baseObj->GetMl(), &nullObject->xformLocal);
  CopyTransform(baseObj->GetMg(), &nullObject->xformGlobal);

  g_scene.nullObjects.push_back(nullObject);

  ExportSplineChildren(baseObj);

  return true;
}

//-----------------------------------------------------------------------------
ImBaseObject::ImBaseObject(melange::BaseObject* melangeObj)
  : melangeObj(melangeObj)
  , parent(g_scene.FindObject(melangeObj->GetUp()))
  , name(CopyString(melangeObj->GetName()))
  , id(ImScene::nextObjectId++)
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
bool melange::AlienCameraObjectData::Execute()
{
  BaseObject* baseObj = (BaseObject*)GetNode();
  const string name = CopyString(baseObj->GetName());

  unique_ptr<ImCamera> camera = make_unique<ImCamera>(baseObj);
  if (!camera->valid)
    return false;

  int projectionType = GetInt32Param(baseObj, CAMERA_PROJECTION);
  if (projectionType != Pperspective)
  {
    LOG(2,
      "Skipping camera (%s) with unsupported projection type (%d)\n",
      name.c_str(),
      projectionType);
    return false;
  }

  // Check if the camera is a target camera
  BaseTag* targetTag = baseObj->GetTag(Ttargetexpression);
  if (!targetTag)
  {
    // Not a target camera, so require the parent object to be a null object
    BaseObject* parent = baseObj->GetUp();
    bool isNullParent = parent && parent->GetType() == OBJECT_NULL;
    if (!isNullParent)
    {
      LOG(1, "Camera's %s parent isn't a null object!\n", name.c_str());
      return false;
    }
  }

  CopyTransform(baseObj->GetMl(), &camera->xformLocal);
  CopyTransform(baseObj->GetMg(), &camera->xformGlobal);

  camera->verticalFov = GetFloatParam(baseObj, CAMERAOBJECT_FOV_VERTICAL);
  camera->nearPlane =
    GetInt32Param(baseObj, CAMERAOBJECT_NEAR_CLIPPING_ENABLE)
    ? max(DEFAULT_NEAR_PLANE, GetFloatParam(baseObj, CAMERAOBJECT_NEAR_CLIPPING))
    : DEFAULT_NEAR_PLANE;
  camera->farPlane = GetInt32Param(baseObj, CAMERAOBJECT_FAR_CLIPPING_ENABLE)
    ? GetFloatParam(baseObj, CAMERAOBJECT_FAR_CLIPPING)
    : DEFAULT_FAR_PLANE;

  if (targetTag)
  {
    BaseObject* targetObj = targetTag->GetDataInstance()->GetObjectLink(TARGETEXPRESSIONTAG_LINK);
    ImCamera* cameraPtr = camera.get();

    // defer finding the target object until all objects have been parsed
    g_deferredFunctions.push_back([=]() {
      cameraPtr->targetObj = g_scene.FindObject(targetObj);
      if (!cameraPtr->targetObj)
      {
        LOG(1, "Unable to find target object: %s", CopyString(targetObj->GetName()).c_str());
        return false;
      }
      return true;
    });
  }


  g_scene.cameras.push_back(camera.release());

  return true;
}

//-----------------------------------------------------------------------------
bool melange::AlienLightObjectData::Execute()
{
  BaseObject* baseObj = (BaseObject*)GetNode();
  const string name = CopyString(baseObj->GetName());

  int lightType = GetInt32Param(baseObj, LIGHT_TYPE);
  int falloffType = GetInt32Param(baseObj, LIGHT_DETAILS_FALLOFF);

  unique_ptr<ImLight> light = make_unique<ImLight>(baseObj);

  CopyTransform(baseObj->GetMl(), &light->xformLocal);
  CopyTransform(baseObj->GetMg(), &light->xformGlobal);

  light->type = lightType;
  light->color = GetVectorParam<Color>(baseObj, LIGHT_COLOR);
  light->intensity = GetFloatParam(baseObj, LIGHT_BRIGHTNESS);

  if (falloffType == LIGHT_DETAILS_FALLOFF_LINEAR)
  {
    light->falloffRadius = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERDISTANCE);
  }

  if (lightType == LIGHT_TYPE_OMNI)
  {
  }
  else if (lightType == LIGHT_TYPE_DISTANT)
  {
  }
  else if (lightType == LIGHT_TYPE_SPOT)
  {
    light->outerAngle = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERANGLE);
  }
  else
  {
    LOG(1, "Unsupported light type: %s\n", name.c_str());
    return true;
  }

  g_scene.lights.push_back(light.release());

  return true;
}

static AlienMaterial* DEFAULT_MATERIAL_PTR = nullptr;
//-----------------------------------------------------------------------------
template <typename Dst, typename Src>
Dst Vector3Coerce(const Src& src)
{
  return Dst(src.x, src.y, src.z);
}

//-----------------------------------------------------------------------------
template <typename Ret, typename Src>
Ret AlphabetIndex(const Src& src, int idx)
{
  switch (idx)
  {
  case 0: return src.a;
  case 1: return src.b;
  case 2: return src.c;
  case 3: return src.d;
  }

  return Ret();
}

//-----------------------------------------------------------------------------
static int VertexIdFromPoly(const CPolygon& poly, int idx)
{
  switch (idx)
  {
  case 0: return poly.a;
  case 1: return poly.b;
  case 2: return poly.c;
  case 3: return poly.d;
  }
  return -1;
}

//-----------------------------------------------------------------------------
void AddIndices(vector<int>* indices, int a, int b, int c)
{
  indices->push_back(a);
  indices->push_back(b);
  indices->push_back(c);
};

//-----------------------------------------------------------------------------
template <typename T>
void AddVector2(vector<float>* out, const T& v, bool flipY)
{
  out->push_back(v.x);
  out->push_back(flipY ? 1 - v.y : v.y);
};

//-----------------------------------------------------------------------------
template <typename T>
void CopyOutVector2(float* dst, const T& src)
{
  dst[0] = src.x;
  dst[1] = src.y;
}

//-----------------------------------------------------------------------------
template <typename T>
void CopyOutVector3(float* dst, const T& src)
{
  dst[0] = src.x;
  dst[1] = src.y;
  dst[2] = src.z;
}

//-----------------------------------------------------------------------------
template <typename T>
void AddVector3(vector<float>* out, const T& v)
{
  out->push_back(v.x);
  out->push_back(v.y);
  out->push_back(v.z);
};

//-----------------------------------------------------------------------------
template <typename T>
void Add3Vector2(vector<float>* out, const T& a, const T& b, const T& c, bool flipY)
{
  AddVector2(out, a, flipY);
  AddVector2(out, b, flipY);
  AddVector2(out, c, flipY);
};

//-----------------------------------------------------------------------------
template <typename T>
void Add3Vector3(vector<float>* out, const T& a, const T& b, const T& c)
{
  AddVector3(out, a);
  AddVector3(out, b);
  AddVector3(out, c);
};

//-----------------------------------------------------------------------------
Vector CalcNormal(
  const Vector& a, const Vector& b, const Vector& c)
{
  Vector e0 = (b - a);
  e0.Normalize();

  Vector e1 = (c - a);
  e1.Normalize();

  return Cross(e0, e1);
}

//-----------------------------------------------------------------------------
template <typename T>
inline bool IsQuad(const T& p)
{
  return p.c != p.d;
}

//-----------------------------------------------------------------------------
static void CalcBoundingSphere(
  const Vector* verts, int vertexCount, Vec3f* outCenter, float* outRadius)
{
  // calc bounding sphere (get center and max radius)
  Vector center(verts[0]);
  for (int i = 1; i < vertexCount; ++i)
  {
    center += verts[i];
  }
  center /= vertexCount;

  float radius = (center - verts[1]).GetSquaredLength();
  for (int i = 2; i < vertexCount; ++i)
  {
    float cand = (center - verts[i]).GetSquaredLength();
    radius = max(radius, cand);
  }

  *outCenter = center;
  *outRadius = sqrtf(radius);
}

//-----------------------------------------------------------------------------
static u32 FnvHash(const char* str, u32 d = 0x01000193)
{
  while (true)
  {
    char c = *str++;
    if (!c)
      return d;
    d = ((d * 0x01000193) ^ c) & 0xffffffff;
  }
}

//-----------------------------------------------------------------------------
struct FatVertex
{
  FatVertex()
  {
    memset(this, 0, sizeof(FatVertex));
  }

  Vector32 pos = Vector32(0, 0, 0);
  Vector32 normal = Vector32(0, 0, 0);
  Vector32 uv = Vector32(0, 0, 0);

  u32 GetHash() const
  {
    if (meta.hash == 0)
      meta.hash = FnvHash((const char*)this, sizeof(FatVertex) - sizeof(meta));
    return meta.hash;
  }

  friend bool operator==(const FatVertex& lhs, const FatVertex& rhs)
  {
    return lhs.pos == rhs.pos && lhs.normal == rhs.normal && lhs.uv == rhs.uv;
  }

  struct Hash
  {
    size_t operator()(const FatVertex& v) const
    {
      return v.GetHash();
    }
  };

  struct
  {
    int id = 0;
    mutable u32 hash = 0;
  } meta;
};

//-----------------------------------------------------------------------------
static void GroupPolysByMaterial(PolygonObject* obj,
  unordered_map<AlienMaterial*, vector<int>>* polysByMaterial)
{
  // Keep track of which polys we've seen, so we can lump all the unseen ones with the first
  // material
  unordered_set<u32> seenPolys;

  AlienMaterial* prevMaterial = nullptr;
  AlienMaterial* firstMaterial = nullptr;

  // For each material found, check if the following tag is a selection tag, in which
  // case record which polys belong to it
  for (BaseTag* btag = obj->GetFirstTag(); btag; btag = btag->GetNext())
  {
    // texture tag
    if (btag->GetType() == Ttexture)
    {
      GeData data;
      prevMaterial = btag->GetParameter(TEXTURETAG_MATERIAL, data)
        ? (AlienMaterial*)data.GetLink()
        : NULL;
      if (!prevMaterial)
        continue;

      // Mark the first material we find, so we can stick all unselected polys in it
      if (!firstMaterial)
        firstMaterial = prevMaterial;
    }

    // Polygon Selection Tag
    if (btag->GetType() == Tpolygonselection && obj->GetType() == Opolygon)
    {
      // skip this selection tag if we don't have a previous material. i don't like relying things
      // just appearing in the correct order, but right now i don't know of a better way
      if (!prevMaterial)
        continue;

      if (BaseSelect* bs = ((SelectionTag*)btag)->GetBaseSelect())
      {
        vector<int>& polysForMaterial = (*polysByMaterial)[prevMaterial];
        for (int i = 0, e = obj->GetPolygonCount(); i < e; ++i)
        {
          if (bs->IsSelected(i))
          {
            polysForMaterial.push_back(i);
            seenPolys.insert(i);
          }
        }
      }

      // reset the previous material flag to avoid double selection tags causing trouble
      prevMaterial = nullptr;
    }
  }

  // if no materials are found, just add them to a dummy material
  if (!firstMaterial)
  {
    vector<int>& polysForMaterial = (*polysByMaterial)[DEFAULT_MATERIAL_PTR];
    for (int i = 0, e = obj->GetPolygonCount(); i < e; ++i)
    {
      polysForMaterial.push_back(i);
    }
  }
  else
  {
    // add all the polygons that aren't selected to the first material
    vector<int>& polysForMaterial = (*polysByMaterial)[firstMaterial];
    for (int i = 0, e = obj->GetPolygonCount(); i < e; ++i)
    {
      if (seenPolys.count(i) == 0)
        polysForMaterial.push_back(i);
    }
  }

  // print polys per material stats.
  for (auto g : (*polysByMaterial))
  {
    AlienMaterial* mat = g.first;
    const char* materialName =
      mat == DEFAULT_MATERIAL_PTR ? "<default>" : CopyString(mat->GetName()).c_str();
    LOG(2, "material: %s, %d polys\n", materialName, (int)g.second.size());
  }
}

//-----------------------------------------------------------------------------
struct FatVertexSupplier
{
  FatVertexSupplier(PolygonObject* polyObj)
  {
    hasNormalsTag = !!polyObj->GetTag(Tnormal);
    hasPhongTag = !!polyObj->GetTag(Tphong);
    hasNormals = hasNormalsTag || hasPhongTag;

    phongNormals = hasPhongTag ? polyObj->CreatePhongNormals() : nullptr;
    normals = hasNormalsTag ? (NormalTag*)polyObj->GetTag(Tnormal) : nullptr;
    uvs = (UVWTag*)polyObj->GetTag(Tuvw);
    uvHandle = uvs ? uvs->GetDataAddressR() : nullptr;
    normalHandle = normals ? normals->GetDataAddressR() : nullptr;

    verts = polyObj->GetPointR();
    polys = polyObj->GetPolygonR();
  }

  ~FatVertexSupplier()
  {
    if (phongNormals)
      _MemFree((void**)&phongNormals);
  }

  int AddVertex(int polyIdx, int vertIdx)
  {
    const CPolygon& poly = polys[polyIdx];

    FatVertex vtx;
    vtx.pos = Vector3Coerce<Vector32>(verts[AlphabetIndex<int>(poly, vertIdx)]);

    if (hasNormals)
    {
      if (hasNormalsTag)
      {
        NormalStruct normal;
        normals->Get(normalHandle, polyIdx, normal);
        vtx.normal = Vector3Coerce<Vector32>(AlphabetIndex<Vector>(normal, vertIdx));
      }
      else if (hasPhongTag)
      {
        vtx.normal = phongNormals[polyIdx * 4 + vertIdx];
      }
    }
    else
    {
      // no normals, so generate polygon normals and use them for all verts
      int idx0 = AlphabetIndex<int>(poly, 0);
      int idx1 = AlphabetIndex<int>(poly, 1);
      int idx2 = AlphabetIndex<int>(poly, 2);
      vtx.normal = Vector3Coerce<Vector32>(CalcNormal(verts[idx0], verts[idx1], verts[idx2]));
    }

    if (uvHandle)
    {
      UVWStruct s;
      UVWTag::Get(uvHandle, polyIdx, s);
      vtx.uv = Vector3Coerce<Vector32>(AlphabetIndex<Vector>(s, vertIdx));
    }

    // Check if the fat vertex already exists
    auto it = fatVertSet.find(vtx);
    if (it != fatVertSet.end())
      return it->meta.id;

    vtx.meta.id = (int)fatVerts.size();
    fatVertSet.insert(vtx);
    fatVerts.push_back(vtx);
    return vtx.meta.id;
  }

  const Vector* verts;
  const CPolygon* polys;

  bool hasNormalsTag;
  bool hasPhongTag;
  bool hasNormals;

  Vector32* phongNormals;
  NormalTag* normals;
  UVWTag* uvs;
  ConstUVWHandle uvHandle;
  ConstNormalHandle normalHandle;

  unordered_set<FatVertex, FatVertex::Hash> fatVertSet;
  vector<FatVertex> fatVerts;
};

template <typename T>
void CopyOutDataStream(const vector<T>& data, const string& streamName, ImMesh* mesh)
{
  size_t used = data.size() * sizeof(T);

  mesh->dataStreams.push_back(ImMesh::DataStream());
  ImMesh::DataStream& s = mesh->dataStreams.back();
  s.name = streamName;
  s.flags = 0;
  s.elemSize = sizeof(T);
  s.data.resize(used);
  memcpy(s.data.data(), data.data(), used);
}

//-----------------------------------------------------------------------------
static void CollectVertices(PolygonObject* polyObj,
  const unordered_map<AlienMaterial*, vector<int>>& polysByMaterial,
  ImMesh* mesh)
{
  int vertexCount = polyObj->GetPointCount();
  if (!vertexCount)
  {
    // TODO: log
    return;
  }

  const Vector* verts = polyObj->GetPointR();
  const CPolygon* polys = polyObj->GetPolygonR();

  CalcBoundingSphere(
    verts, vertexCount, &mesh->boundingSphere.center, &mesh->boundingSphere.radius);

  FatVertexSupplier fatVtx(polyObj);
  int startIdx = 0;

  vector<int> indexStream;

  int maxVtx = 0;

  // Create the material groups, where each group contains polygons that share the same material
  for (const pair<AlienMaterial*, vector<int>>& kv : polysByMaterial)
  {
    ImMesh::MaterialGroup mg;
    ImMaterial* mat = g_scene.FindMaterial(kv.first);
    mg.materialId = mat ? mat->id : ~0;
    mg.startIndex = startIdx;

    // iterate over all the polygons in the material group, and collect the vertices
    for (int polyIdx : kv.second)
    {
      int idx0 = fatVtx.AddVertex(polyIdx, 0);
      int idx1 = fatVtx.AddVertex(polyIdx, 1);
      int idx2 = fatVtx.AddVertex(polyIdx, 2);

      maxVtx = max(maxVtx, idx0);
      maxVtx = max(maxVtx, idx1);
      maxVtx = max(maxVtx, idx2);

      indexStream.push_back(idx0);
      indexStream.push_back(idx1);
      indexStream.push_back(idx2);
      startIdx += 3;

      if (IsQuad(polys[polyIdx]))
      {
        int idx3 = fatVtx.AddVertex(polyIdx, 3);
        maxVtx = max(maxVtx, idx3);

        indexStream.push_back(idx0);
        indexStream.push_back(idx2);
        indexStream.push_back(idx3);
        startIdx += 3;
      }
    }
    mg.numIndices = startIdx - mg.startIndex;
    mesh->materialGroups.push_back(mg);
  }

  // copy the data over from the fat vertices
  int numFatVerts = (int)fatVtx.fatVerts.size();

  vector<Vector32> posStream;
  vector<Vector32> normalStream;
  vector<Vec2> uvStream;

  for (int i = 0; i < numFatVerts; ++i)
  {
    posStream.push_back(fatVtx.fatVerts[i].pos);
    normalStream.push_back(fatVtx.fatVerts[i].normal);
    if (fatVtx.uvHandle)
    {
      uvStream.push_back(Vec2{ fatVtx.fatVerts[i].uv.x, fatVtx.fatVerts[i].uv.y });
    }
  }

  if (maxVtx < 65536)
  {
    vector<u16> indexStream16(indexStream.size());
    for (size_t i = 0; i < indexStream.size(); ++i)
    {
      indexStream16[i] = (u16)(indexStream[i]);
    }
    CopyOutDataStream(indexStream16, "index16", mesh);
  }
  else
  {
    CopyOutDataStream(indexStream, "index32", mesh);
  }
  CopyOutDataStream(posStream, "pos", mesh);
  CopyOutDataStream(normalStream, "normal", mesh);

  if (uvStream.size())
  {
    CopyOutDataStream(uvStream, "uv", mesh);
  }
}

//-----------------------------------------------------------------------------
bool AlienPolygonObjectData::Execute()
{
  BaseObject* baseObj = (BaseObject*)GetNode();
  PolygonObject* polyObj = (PolygonObject*)baseObj;

  unique_ptr<ImMesh> mesh = make_unique<ImMesh>(baseObj);
  if (!mesh->valid)
    return false;

  unordered_map<AlienMaterial*, vector<int>> polysByMaterial;
  GroupPolysByMaterial(polyObj, &polysByMaterial);
  CollectVertices(polyObj, polysByMaterial, mesh.get());

  CopyTransform(polyObj->GetMl(), &mesh->xformLocal);
  CopyTransform(polyObj->GetMg(), &mesh->xformGlobal);

  g_scene.meshes.push_back(mesh.release());

  return true;
}
