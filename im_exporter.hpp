#pragma once

namespace melange
{
  //-----------------------------------------------------------------------------
  class AlienPrimitiveObjectData : public NodeData
  {
    INSTANCEOF(AlienPrimitiveObjectData, NodeData)
  public:
    virtual Bool Execute();
  };

  //-----------------------------------------------------------------------------
  class AlienNullObjectData : public NodeData
  {
    INSTANCEOF(AlienNullObjectData, NodeData)
  public:
    virtual Bool Execute();
  };

  //-----------------------------------------------------------------------------
  class AlienCameraObjectData : public CameraObjectData
  {
    INSTANCEOF(AlienCameraObjectData, CameraObjectData)
  public:
    virtual Bool Execute();
  };

  //-----------------------------------------------------------------------------
  class AlienLightObjectData : public LightObjectData
  {
    INSTANCEOF(AlienLightObjectData, LightObjectData)
  public:
    virtual Bool Execute();
  };

  //-----------------------------------------------------------------------------
  class AlienMaterial : public Material
  {
    INSTANCEOF(AlienMaterial, Material)
  public:
    virtual Bool Execute() { return true; }
  };

  //-----------------------------------------------------------------------------
  class AlienPolygonObjectData : public PolygonObjectData
  {
    INSTANCEOF(AlienPolygonObjectData, PolygonObjectData)
  public:
    virtual Bool Execute();
  };

}
