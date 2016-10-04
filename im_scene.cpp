#include "im_scene.hpp"
#include "exporter.hpp"
#include "exporter_utils.hpp"

extern ExportInstance g_ExportInstance;

//-----------------------------------------------------------------------------
ImScene::~ImScene()
{

}

//-----------------------------------------------------------------------------
ImBaseObject::ImBaseObject(melange::BaseObject* melangeObj)
  : melangeObj(melangeObj)
  , parent(g_ExportInstance.scene->FindObject(melangeObj->GetUp()))
  , name(CopyString(melangeObj->GetName()))
  , id(ImScene::nextObjectId++)
{
  g_ExportInstance.Log(1, "Exporting: %s\n", name.c_str());
  melange::BaseObject* melangeParent = melangeObj->GetUp();
  if ((melangeParent != nullptr) ^ (parent != nullptr))
  {
    g_ExportInstance.Log(1,
      "  Unable to find parent! (%s)\n",
      melangeParent ? CopyString(melangeParent->GetName()).c_str() : "");
    valid = false;
  }

  // add the object to its parent's children
  if (melangeParent && parent)
  {
    g_ExportInstance.deferredFunctions.push_back([this]() {
      parent->children.push_back(this);
      return true;
    });
  }
  g_ExportInstance.scene->melangeToImObject[melangeObj] = this;
  g_ExportInstance.scene->imObjectToMelange[this] = melangeObj;
}

//------------------------------------------------------------------------------
const ImMesh::DataStream* ImMesh::StreamByType(ImMesh::DataStream::Type type) const
{
  for (size_t i = 0; i < dataStreams.size(); ++i)
  {
    if (dataStreams[i].type == type)
    {
      return &dataStreams[i];
    }
  }
  return nullptr;
}
