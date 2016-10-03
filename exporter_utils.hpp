#pragma once

struct ImTransform;
struct ImTrack;

namespace melange
{
  class AlienBaseDocument;
}

//-----------------------------------------------------------------------------
struct ImTransform;
struct ImBaseObject;
void CopyTransform(const melange::Matrix& mtx, ImTransform* xform);
void CopyBaseTransform(melange::BaseObject* melangeObj, ImBaseObject* imObj);
void CopyMatrix(const melange::Matrix& mtx, float* out);

void CollectionAnimationTracksForObj(melange::BaseList2D* bl, vector<ImTrack>* tracks);
void CollectMaterials(melange::AlienBaseDocument* c4dDoc);
void CollectMaterials2(melange::AlienBaseDocument* c4dDoc);

string CopyString(const melange::String& str);
string ReplaceAll(const string& str, char toReplace, char replaceWith);


//-----------------------------------------------------------------------------
template <typename R, typename T>
R GetVectorParam(T* obj, int paramId)
{
  melange::GeData data;
  obj->GetParameter(paramId, data);
  melange::Vector v = data.GetVector();
  return R(v.x, v.y, v.z);
}

//-----------------------------------------------------------------------------
template <typename R, typename T>
R GetColorParam(T* obj, int paramId)
{
  melange::GeData data;
  obj->GetParameter(paramId, data);
  melange::Vector v = data.GetVector();
  return R{v.x, v.y, v.z, 1};
}

//-----------------------------------------------------------------------------
template <typename T>
float GetFloatParam(T* obj, int paramId)
{
  melange::GeData data;
  obj->GetParameter(paramId, data);
  return (float)data.GetFloat();
}

//-----------------------------------------------------------------------------
template <typename T>
int GetInt32Param(T* obj, int paramId)
{
  melange::GeData data;
  obj->GetParameter(paramId, data);
  return (int)data.GetInt32();
}

//-----------------------------------------------------------------------------
template <typename T>
int GetInt32Data(T* obj, int paramId)
{
  melange::GeData data = obj->GetData(paramId);
  return (int)data.GetInt32();
}

//-----------------------------------------------------------------------------
template<typename T, typename U>
T* SharedPtrCast(shared_ptr<U>& ptr)
{
  return static_cast<T*>(ptr.get());
}

//-----------------------------------------------------------------------------
void GetChildren(melange::BaseObject* obj, vector<melange::BaseObject*>* children);

struct IdGenerator
{
  IdGenerator(int initialId = 0) : _nextId(initialId) {}
  int NextId() { return _nextId++; }
  int _nextId;
};

//------------------------------------------------------------------------------
template <typename T>
void operator+=(vector<T>& lhs, const vector<T>& rhs)
{
  lhs.insert(lhs.end(), rhs.begin(), rhs.end());
}

extern IdGenerator g_ObjectId;
extern IdGenerator g_MaterialId;
extern unordered_map<int, melange::BaseMaterial*> g_MaterialIdToObj;

