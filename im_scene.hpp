#pragma once
#include "exporter_types.hpp"

//------------------------------------------------------------------------------
struct ImSphere
{
  Vec3 center;
  float radius;
};

//------------------------------------------------------------------------------
struct ImAABB
{
  Vec3 center;
  Vec3 extents;
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
struct ImTransform
{
  Vec3 pos;
  Vec3 rot;
  Vec4 quat;
  Vec3 scale;
};

//------------------------------------------------------------------------------
struct ImBaseObject
{
  ImBaseObject(melange::BaseObject* melangeObj);

  melange::BaseObject* melangeObj = nullptr;
  ImBaseObject* parent = nullptr;

  ImTransform xformLocal;
  ImTransform xformGlobal;
  string name;
  u32 id = ~0u;
  bool valid = true;

  vector<ImTrack> animTracks;
  vector<ImBaseObject*> children;
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

  union
  {
    struct
    {
      float outerAngle;
    };

    struct 
    {
      string areaShape;
      float areaSizeX;
      float areaSizeY;
      float areaSizeZ;
    };
  };
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
  string texture;
  float brightness;
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

  vector<DataStream> dataStreams;

  vector<MaterialGroup> materialGroups;
  vector<u32> selectedEdges;

  ImSphere boundingSphere;
  ImAABB aabb;
};

//------------------------------------------------------------------------------
struct ImScene
{
  ImBaseObject* FindObject(melange::BaseObject* obj);
  ImMaterial* FindMaterial(melange::BaseMaterial* mat);
  vector<ImMesh*> meshes;
  vector<ImCamera*> cameras;
  vector<ImNullObject*> nullObjects;
  vector<ImLight*> lights;
  vector<ImMaterial*> materials;
  vector<ImSpline*> splines;
  unordered_map<melange::BaseObject*, ImBaseObject*> melangeToImObject;

  ImSphere boundingSphere;

  static u32 nextObjectId;
};
