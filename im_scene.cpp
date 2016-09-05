#include "im_scene.hpp"

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
