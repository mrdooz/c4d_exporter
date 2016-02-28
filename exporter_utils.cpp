#include "exporter.hpp"

//-----------------------------------------------------------------------------
void CopyTransform(const melange::Matrix& mtx, exporter::Transform* xform)
{
  xform->pos = mtx.off;
  xform->rot = melange::MatrixToHPB(mtx, melange::ROTATIONORDER_HPB);
  xform->scale = melange::Vector(Len(mtx.v1), Len(mtx.v2), Len(mtx.v3));
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
