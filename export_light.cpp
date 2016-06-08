#include "export_light.hpp"
#include "exporter_utils.hpp"
#include "exporter.hpp"
#include "export_misc.hpp"

//static unordered_map<int, scene::Light::FalloffType> FALLOFF_LOOKUP = {
//  { melange::LIGHT_DETAILS_FALLOFF_NONE, scene::Light::FalloffType::None },
//  { melange::LIGHT_DETAILS_FALLOFF_LINEAR, scene::Light::FalloffType::Linear } };

//-----------------------------------------------------------------------------
bool melange::AlienLightObjectData::Execute()
{
  BaseObject* baseObj = (BaseObject*)GetNode();
  const string name = CopyString(baseObj->GetName());

  int lightType = GetInt32Param(baseObj, LIGHT_TYPE);
  int falloffType = GetInt32Param(baseObj, LIGHT_DETAILS_FALLOFF);

#if WITH_SCENE_1
  if (lightType == LIGHT_TYPE_OMNI)
  {
  }
  else if (lightType == LIGHT_TYPE_DISTANT)
  {
  }
  else if (lightType == LIGHT_TYPE_SPOT)
  {
  }
  else
  {
    LOG(1, "Unsupported light type: %s\n", name.c_str());
    return true;
  }

  exporter::Light* light = new exporter::Light(baseObj);
#if WITH_XFORM_MTX
  CopyMatrix(baseObj->GetMl(), light->mtxLocal);
  CopyMatrix(baseObj->GetMg(), light->mtxGlobal);
#endif

  CopyTransform(baseObj->GetMl(), &light->xformLocal);
  CopyTransform(baseObj->GetMg(), &light->xformGlobal);

  light->type = lightType;
  light->color = GetVectorParam<exporter::Color>(baseObj, LIGHT_COLOR);
  light->intensity = GetFloatParam(baseObj, LIGHT_BRIGHTNESS);

  if (falloffType == LIGHT_DETAILS_FALLOFF_LINEAR)
  {
    light->falloffRadius = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERDISTANCE);
  }

  if (lightType == LIGHT_TYPE_DISTANT)
  {
  }
  else if (lightType == LIGHT_TYPE_SPOT)
  {
    light->outerAngle = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERANGLE);
  }

  g_scene.lights.push_back(light);
#else
  shared_ptr<scene::Light> light;
  if (lightType == LIGHT_TYPE_OMNI)
  {
    light = make_shared<scene::Omni>();
    g_Scene2.omnis.push_back(static_pointer_cast<scene::Omni>(light));
  }
  else if (lightType == LIGHT_TYPE_DISTANT)
  {
    light = make_shared<scene::Directional>();
    g_Scene2.directionals.push_back(static_pointer_cast<scene::Directional>(light));
  }
  else if (lightType == LIGHT_TYPE_SPOT)
  {
    light = make_shared<scene::Spot>();
    auto spot = static_pointer_cast<scene::Spot>(light);
    spot->outerAngle = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERANGLE);
    g_Scene2.spots.push_back(spot);
  }
  else
  {
    LOG(1, "Unsupported light type: %s\n", name.c_str());
    return true;
  }

  CopyTransform(baseObj->GetMl(), &light->xformLocal);
  CopyTransform(baseObj->GetMg(), &light->xformGlobal);

  light->color = GetColorParam<scene::Color>(baseObj, LIGHT_COLOR);
  light->intensity = GetFloatParam(baseObj, LIGHT_BRIGHTNESS);

  if (falloffType == LIGHT_DETAILS_FALLOFF_LINEAR)
  {
    light->falloffRadius = GetFloatParam(baseObj, LIGHT_DETAILS_OUTERDISTANCE);
  }

#endif

  return true;
}
