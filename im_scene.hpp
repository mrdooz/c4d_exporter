#pragma once
#include "exporter_types.hpp"

//------------------------------------------------------------------------------
struct ImSphere
{
  vec3 center;
  float radius;
};

//------------------------------------------------------------------------------
struct ImAABB
{
  ImAABB(const vec3& minValue, const vec3& maxValue) : minValue(minValue), maxValue(maxValue)
  {
  }
  ImAABB() : minValue{+FLT_MAX, +FLT_MAX, +FLT_MAX}, maxValue{-FLT_MAX, -FLT_MAX, -FLT_MAX}
  {
  }

  ImAABB Extend(const ImAABB& x)
  {
    return ImAABB(Min(minValue, x.minValue), Max(maxValue, x.maxValue));
  }

  vec3 minValue;
  vec3 maxValue;
};

//------------------------------------------------------------------------------
struct ImMeshFace
{
  union
  {
    struct
    {
      int a, b, c;
    };
    int vtx[3];
  };
};

//------------------------------------------------------------------------------
struct ImMeshVertex
{
  float x, y, z;
};

//------------------------------------------------------------------------------
struct ImGeometry
{
  vector<ImMeshFace> faces;
  vector<ImMeshVertex> vertices;
  vector<vec3> faceNormals;
  unordered_map<pair<int, int>, vec3> edgeNormals;
  vector<vec3> vertexNormals;
  ImAABB aabb;
};

//------------------------------------------------------------------------------
struct ImKeyframe
{
  int frame;
  float value;
};

//------------------------------------------------------------------------------
struct ImCurve
{
  string name;
  vector<ImKeyframe> keyframes;
};

//------------------------------------------------------------------------------
struct ImTrack
{
  string name;
  vector<ImCurve> curves;
};

//------------------------------------------------------------------------------
struct ImSampledTrack
{
  string name;
  vector<float> values;
};

//------------------------------------------------------------------------------
struct ImTransform
{
  melange::Matrix mtx;
  vec3 pos;
  vec3 rot;
  Vec4 quat;
  vec3 scale;
};

//------------------------------------------------------------------------------
struct ImBaseObject
{
  ImBaseObject(melange::BaseObject* melangeObj);
  virtual ~ImBaseObject() {}

  melange::BaseObject* melangeObj = nullptr;
  ImBaseObject* parent = nullptr;

  ImTransform xformLocal;
  ImTransform xformGlobal;
  string name;
  u32 id = ~0u;
  bool valid = true;

  vector<ImSampledTrack> sampledAnimTracks;
  vector<ImTrack> animTracks;
  vector<ImBaseObject*> children;
};

//------------------------------------------------------------------------------
struct ImPrimitive : public ImBaseObject
{
  enum class Type
  {
    Cube,
    Sphere,
  };

  ImPrimitive(Type type, melange::BaseObject* melangeObj) : ImBaseObject(melangeObj), type(type) {}
  Type type;
};

//------------------------------------------------------------------------------
struct ImPrimitiveCube : public ImPrimitive
{
  ImPrimitiveCube(melange::BaseObject* melangeObj) : ImPrimitive(ImPrimitive::Type::Cube, melangeObj) {}

  vec3 size;
};

//------------------------------------------------------------------------------
struct ImLight : public ImBaseObject
{
  enum class Type
  {
    Omni,
    Spot,
    Distant,
    Area,
  };

  ImLight(melange::BaseObject* melangeObj) : ImBaseObject(melangeObj) {}

  Type type;
  Color color;
  float intensity;

  int falloffType;
  float falloffRadius;

  float outerAngle;
  string areaShape;
  float areaSizeX;
  float areaSizeY;
  float areaSizeZ;
};

//------------------------------------------------------------------------------
struct ImNullObject : public ImBaseObject
{
  ImNullObject(melange::BaseObject* melangeObj) : ImBaseObject(melangeObj) {}
};

//------------------------------------------------------------------------------
struct ImCamera : public ImBaseObject
{
  ImCamera(melange::BaseObject* melangeObj) : ImBaseObject(melangeObj) {}

  ImBaseObject* targetObj = nullptr;
  float verticalFov;
  float nearPlane, farPlane;
};

//------------------------------------------------------------------------------
struct ImMaterialComponent
{
  string name;
  Color color;
  float brightness;
  melange::BaseShader* shader;
};

//------------------------------------------------------------------------------
struct ImMaterial
{
  ImMaterial() : mat(nullptr), id(nextId++) {}

  string name;
  melange::BaseMaterial* mat;
  u32 id;

  vector<ImMaterialComponent> components;

  static u32 nextId;
};

//------------------------------------------------------------------------------
struct ImSpline : public ImBaseObject
{
  ImSpline(melange::SplineObject* melangeObj) : ImBaseObject(melangeObj) {}
  int type = 0;
  vector<float> points;
  bool isClosed = 0;
};

//------------------------------------------------------------------------------
struct ImMesh : public ImBaseObject
{
  ImMesh(melange::BaseObject* melangeObj) : ImBaseObject(melangeObj) {}

  struct MaterialGroup
  {
    int materialId;
    u32 startIndex = ~0u;
    u32 indexCount = ~0u;
  };

  struct DataStream
  {
    enum class Type
    {
      Index16,
      Index32,
      Pos,
      Normal,
      UV,
    };

    size_t NumElems() const { return data.size() / elemSize; }

    Type type;
    u32 flags = 0;
    int elemSize;
    vector<char> data;
  };

  const DataStream* StreamByType(DataStream::Type type) const;

  vector<DataStream> dataStreams;

  vector<MaterialGroup> materialGroups;
  vector<u32> selectedEdges;

  ImSphere boundingSphere;
  ImAABB aabb;
  ImGeometry geometry ;
};

//------------------------------------------------------------------------------
struct ImScene
{
  ~ImScene();
  ImBaseObject* FindObject(melange::BaseObject* obj);
  melange::BaseObject* FindMelangeObject(ImBaseObject* obj);
  ImMaterial* FindMaterial(melange::BaseMaterial* mat);
  vector<ImPrimitive*> primitives;
  vector<ImMesh*> meshes;
  vector<ImCamera*> cameras;
  vector<ImNullObject*> nullObjects;
  vector<ImLight*> lights;
  vector<ImMaterial*> materials;
  vector<ImSpline*> splines;
  unordered_map<melange::BaseObject*, ImBaseObject*> melangeToImObject;
  unordered_map<ImBaseObject*, melange::BaseObject*> imObjectToMelange;

  ImSphere boundingSphere;
  ImAABB boundingBox;

  float startTime, endTime;
  int fps;

  static u32 nextObjectId;
};
