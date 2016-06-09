//#include "export_light.hpp"
//#include "exporter_utils.hpp"
//#include "exporter.hpp"
//#include "export_misc.hpp"
//
////-----------------------------------------------------------------------------
//bool melange::AlienLightObjectData::Execute()
//{
//  BaseObject* baseObj = (BaseObject*)GetNode();
//  const string name = CopyString(baseObj->GetName());
//
//  int lightType = GetInt32Param(baseObj, LIGHT_TYPE);
//  int falloffType = GetInt32Param(baseObj, LIGHT_DETAILS_FALLOFF);
//
//  unique_ptr<ImLight> light = make_unique<ImLight>(baseObj);
//
//  CopyTransform(baseObj->GetMl(), &light->xformLocal);
//  CopyTransform(baseObj->GetMg(), &light->xformGlobal);
//
//  light->type = lightType;
//  light->color = GetVectorParam<Color>(baseObj, LIGHT_COLOR);
//  light->intensity = GetFloatParam(baseObj, LIGHT_BRIGHTNESS);
//
//  if (falloffType == LIGHT_DETAILS_FALLOFF_LINEAR)
//  {
//    light->falloffRadius = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERDISTANCE);
//  }
//
//  if (lightType == LIGHT_TYPE_OMNI)
//  {
//  }
//  else if (lightType == LIGHT_TYPE_DISTANT)
//  {
//  }
//  else if (lightType == LIGHT_TYPE_SPOT)
//  {
//    light->outerAngle = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERANGLE);
//  }
//  else
//  {
//    LOG(1, "Unsupported light type: %s\n", name.c_str());
//    return true;
//  }
//
//  g_scene.lights.push_back(light.release());
//
//  return true;
//}
