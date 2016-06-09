//#include "export_misc.hpp"
//#include "exporter_utils.hpp"
//#include "melange_helpers.hpp"
//#include "exporter.hpp"
//
////-----------------------------------------------------------------------------
//static void ExportSpline(melange::BaseObject* obj)
//{
//  melange::SplineObject* splineObject = static_cast<melange::SplineObject*>(obj);
//  string splineName = CopyString(splineObject->GetName());
//
//  melange::SPLINETYPE splineType = splineObject->GetSplineType();
//  bool isClosed = splineObject->GetIsClosed();
//  int pointCount = splineObject->GetPointCount();
//  const melange::Vector* points = splineObject->GetPointR();
//
//  ImSpline* s = new ImSpline(splineObject);
//  s->type = splineType;
//  s->isClosed = isClosed;
//
//  s->points.reserve(pointCount * 3);
//  for (int i = 0; i < pointCount; ++i)
//  {
//    s->points.push_back(points[i].x);
//    s->points.push_back(points[i].y);
//    s->points.push_back(points[i].z);
//  }
//
//  g_scene.splines.push_back(s);
//}
//
////-----------------------------------------------------------------------------
//static void ExportSplineChildren(melange::BaseObject* baseObj)
//{
//  // Export spline objects that are children to the null object
//  vector<melange::BaseObject*> children;
//  GetChildren(baseObj, &children);
//  for (melange::BaseObject* obj : children)
//  {
//    int type = obj->GetType();
//    switch (type)
//    {
//      case Ospline:
//      {
//        ExportSpline(obj);
//        break;
//      }
//    }
//  }
//}
//
////-----------------------------------------------------------------------------
//bool melange::AlienPrimitiveObjectData::Execute()
//{
//  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();
//
//  string name(CopyString(baseObj->GetName()));
//  LOG(1, "Skipping primitive object: %s\n", name.c_str());
//  return true;
//}
//
////-----------------------------------------------------------------------------
//bool melange::AlienNullObjectData::Execute()
//{
//  melange::BaseObject* baseObj = (melange::BaseObject*)GetNode();
//  const string name = CopyString(baseObj->GetName());
//
//  ImNullObject* nullObject = new ImNullObject(baseObj);
//
//  CopyTransform(baseObj->GetMl(), &nullObject->xformLocal);
//  CopyTransform(baseObj->GetMg(), &nullObject->xformGlobal);
//
//  g_scene.nullObjects.push_back(nullObject);
//
//  ExportSplineChildren(baseObj);
//
//  return true;
//}
//
////-----------------------------------------------------------------------------
//ImBaseObject::ImBaseObject(melange::BaseObject* melangeObj)
//    : melangeObj(melangeObj)
//    , parent(g_scene.FindObject(melangeObj->GetUp()))
//    , name(CopyString(melangeObj->GetName()))
//    , id(ImScene::nextObjectId++)
//{
//  LOG(1, "Exporting: %s\n", name.c_str());
//  melange::BaseObject* melangeParent = melangeObj->GetUp();
//  if ((melangeParent != nullptr) ^ (parent != nullptr))
//  {
//    LOG(1,
//        "  Unable to find parent! (%s)\n",
//        melangeParent ? CopyString(melangeParent->GetName()).c_str() : "");
//    valid = false;
//  }
//
//  g_scene.objMap[melangeObj] = this;
//}
//
