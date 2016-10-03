#include "json_exporter.hpp"
#include <dlib/json_writer.hpp>
#include "exporter_utils.hpp"
#include "sdf_gen.hpp"
#include "bit_utils.hpp"

static unordered_map<ImBaseObject*, string> _objectToNodeName;
vector<char> buffer;

struct StreamData
{
  const char* type;
  const char* componentType;
  size_t elementSize;
};

static unordered_map<ImMesh::DataStream::Type, StreamData> streamToStreamData = {
    {ImMesh::DataStream::Type::Index16, StreamData{"u16", "scalar", 2}},
    {ImMesh::DataStream::Type::Index32, StreamData{"u32", "scalar", 4}},
    {ImMesh::DataStream::Type::Pos, StreamData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::Normal, StreamData{"r32", "vec3", 12}},
    {ImMesh::DataStream::Type::UV, StreamData{"r32", "vec2", 8}},
};

static unordered_map<ImMesh::DataStream::Type, string> streamTypeToString = {
    {ImMesh::DataStream::Type::Index16, "index"},
    {ImMesh::DataStream::Type::Index32, "index"},
    {ImMesh::DataStream::Type::Pos, "pos"},
    {ImMesh::DataStream::Type::Normal, "normal"},
    {ImMesh::DataStream::Type::UV, "uv"},
};

static unordered_map<ImLight::Type, string> lightTypeToString = {
    {ImLight::Type::Omni, "omni"},
    {ImLight::Type::Spot, "spot"},
    {ImLight::Type::Distant, "distant"},
    {ImLight::Type::Area, "area"},
};

//------------------------------------------------------------------------------
void JsonExporter::AddToBuffer(const char* data, size_t len, const string& name, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, name, JsonWriter::CompoundType::Object);
  w->Emit("offset", buffer.size());
  w->Emit("size", len);
  buffer.insert(buffer.end(), data, data + len);
}

//------------------------------------------------------------------------------
void JsonExporter::ExportBase(ImBaseObject* obj, JsonWriter* w)
{
  w->Emit("name", obj->name);
  w->Emit("id", obj->id);

  auto fnWriteXform = [=](const string& name, const ImTransform& xform) {
    JsonWriter::JsonScope s(w, name, JsonWriter::CompoundType::Object);
    w->EmitArray("pos", {xform.pos.x, xform.pos.y, xform.pos.z});
    w->EmitArray("rot", {xform.rot.x, xform.rot.y, xform.rot.z});
    w->EmitArray("quat", {xform.quat.x, xform.quat.y, xform.quat.z, xform.quat.w});
    w->EmitArray("scale", {xform.scale.x, xform.scale.y, xform.scale.z});
  };

  fnWriteXform("xformLocal", obj->xformLocal);
  fnWriteXform("xformGlobal", obj->xformGlobal);

  ExportAnimationTracks(obj, w);

}

//------------------------------------------------------------------------------
void JsonExporter::ExportNullObjects(const vector<ImNullObject*>& nullObjects, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "nullObjects", JsonWriter::CompoundType::Object);

  for (ImNullObject* obj : nullObjects)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[obj], JsonWriter::CompoundType::Object);
    ExportBase(obj, w);
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportCameras(const vector<ImCamera*>& cameras, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "cameras", JsonWriter::CompoundType::Object);

  for (ImCamera* cam : cameras)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[cam], JsonWriter::CompoundType::Object);
    ExportBase(cam, w);
    w->Emit("nearPlane", cam->nearPlane);
    w->Emit("farPlane", cam->farPlane);
    w->Emit("fovV", cam->verticalFov);

    if (cam->targetObj)
    {
      w->Emit("type", "target");
    }
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportLights(const vector<ImLight*>& lights, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "lights", JsonWriter::CompoundType::Object);

  for (ImLight* light : lights)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[light], JsonWriter::CompoundType::Object);
    ExportBase(light, w);

    w->Emit("type", lightTypeToString[light->type]);
    switch (light->type)
    {
      case ImLight::Type::Area:
      {
        w->Emit("areaShape", light->areaShape);
        w->Emit("sizeX", light->areaSizeX);
        w->Emit("sizeY", light->areaSizeY);
        if (light->areaShape == "sphere")
          w->Emit("sizeZ", light->areaSizeZ);

        break;
      }
    }
  }
}


//------------------------------------------------------------------------------
void JsonExporter::ExportWorldGeometry(JsonWriter* w)
{
  vector<ImMeshFace> faces;
  vector<ImMeshVertex> vertices;
  vector<vec3> faceNormals;
  vector<vec3> vertexNormals;

  int faceOfs = 0;
  for (ImMesh* mesh : instance.scene.meshes)
  {
    vertices += mesh->geometry.vertices;
    vertexNormals += mesh->geometry.vertexNormals;
    faceNormals += mesh->geometry.faceNormals;

    faces.reserve(faces.size() + mesh->geometry.faces.size());
    for (const ImMeshFace& face : mesh->geometry.faces)
      faces.push_back(ImMeshFace{face.a + faceOfs, face.b + faceOfs, face.c + faceOfs});
    faceOfs += (int)mesh->geometry.vertices.size();
  }

  w->Emit("numIndices", faces.size() * 3);
  w->Emit("numVertices", vertices.size());

  AddToBuffer(faces, "indexData", w);
  AddToBuffer(vertices, "vertexData", w);
  AddToBuffer(vertexNormals, "vertexNormalData", w);
  AddToBuffer(faceNormals, "faceNormalData", w);
}

//------------------------------------------------------------------------------
void JsonExporter::ExportMeshData(ImMesh* mesh, JsonWriter* w)
{
  // save the stream data
  {
    JsonWriter::JsonScope s(w, "streams", JsonWriter::CompoundType::Object);
    for (const ImMesh::DataStream& dataStream : mesh->dataStreams)
    {
      JsonWriter::JsonScope s(w, streamTypeToString[dataStream.type], JsonWriter::CompoundType::Object);

      auto it = streamToStreamData.find(dataStream.type);
      if (it == streamToStreamData.end())
      {
        // LOG: unknown data stream type
        continue;
      }

      const StreamData& data = it->second;
      w->Emit("type", data.componentType);
      w->Emit("subtype", data.type);
      w->Emit("elementSize", data.elementSize);
      w->Emit("numElements", dataStream.NumElems());

      // copy the stream data to the buffer
      AddToBuffer(dataStream.data, "data", w);
    }
  }

  {
    // write the materialGroups
    JsonWriter::JsonScope s(w, "materialGroups", JsonWriter::CompoundType::Array);
    for (const ImMesh::MaterialGroup& m : mesh->materialGroups)
    {
      JsonWriter::JsonScope s(w, JsonWriter::CompoundType::Object);
      w->Emit("materialId", m.materialId);
      w->Emit("startIndex", m.startIndex);
      w->Emit("indexCount", m.indexCount);
    }
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportAnimationTracks(ImBaseObject* obj, JsonWriter* w)
{
  if (obj->sampledAnimTracks.empty())
    return;

  {
    JsonWriter::JsonScope s(w, "animTracks", JsonWriter::CompoundType::Object);

    for (ImSampledTrack& track : obj->sampledAnimTracks)
    {
      JsonWriter::JsonScope s(w, track.name, JsonWriter::CompoundType::Object);

      // determine best encoding
      float minValue = +FLT_MAX;
      float maxValue = -FLT_MAX;

      for (float v : track.values)
      {
        minValue = min(minValue, v);
        maxValue = max(maxValue, v);
      }

      float span = maxValue - minValue;

      vector<float> normalizedValues(track.values.size());
      for (size_t i = 0; i < track.values.size(); ++i)
      {
        normalizedValues[i] = (track.values[i] - minValue) / span;
      }

      float maxErr = 0.0001f;
      u32 numBits = 8;
      for (; numBits < 32; ++numBits)
      {
        bool precisionError = false;
        for (float v : normalizedValues)
        {
          int m = 1 << (numBits - 1);
          float err = fabs(v - (float)(int(m * v)) / m);
          if (err > maxErr)
          {
            precisionError = true;
            break;
          }
        }

        if (!precisionError)
          break;
      }

      vector<u8> data;
      BitWriter writer;
      for (float v : normalizedValues)
      {
        int m = 1 << (numBits - 1);
        u32 vv = u32(m * v);
        writer.Write(vv, numBits);
      }

      w->Emit("fps", instance.scene.fps);
      w->Emit("numKeys", track.values.size());
      w->Emit("minValue", minValue);
      w->Emit("maxValue", maxValue);
      w->Emit("bitLength", numBits);
      writer.CopyOut(&data);

      AddToBuffer(data, "data", w);
    }
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportMeshes(const vector<ImMesh*>& meshes, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "meshes", JsonWriter::CompoundType::Object);

  for (ImMesh* mesh : meshes)
  {
    JsonWriter::JsonScope s(w, _objectToNodeName[mesh], JsonWriter::CompoundType::Object);

    ExportBase(mesh, w);
    ExportMeshData(mesh, w);
    {
      JsonWriter::JsonScope s(w, "boundingSphere", JsonWriter::CompoundType::Object);
      w->Emit("radius", mesh->boundingSphere.radius);
      auto& center = mesh->boundingSphere.center;
      w->EmitArray("center", { center.x, center.y, center.z });
    }

    {
      JsonWriter::JsonScope s(w, "boundingBox", JsonWriter::CompoundType::Object);
      const vec3& center = (mesh->aabb.maxValue + mesh->aabb.minValue) / 2;
      const vec3& extents = (mesh->aabb.maxValue - mesh->aabb.minValue) / 2;
      w->EmitArray("center", { center.x, center.y, center.z });
      w->EmitArray("extents", { extents.x, extents.y, extents.z });
    }
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportPrimitives(const vector<ImPrimitive*>& primitives, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "primitives", JsonWriter::CompoundType::Object);

  for (ImPrimitive* prim : primitives)
  {
    switch (prim->type)
    {
      case ImPrimitive::Type::Cube:
      {
        const ImPrimitiveCube* c = static_cast<const ImPrimitiveCube*>(prim);
        JsonWriter::JsonScope s(w, "cube", JsonWriter::CompoundType::Object);
        ExportBase(prim, w);
        w->EmitArray("size", {c->size.x, c->size.y, c->size.z});
        break;
      }
    }
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportSceneInfo(JsonWriter* w)
{
  vector<ImBaseObject*> allObjects;

  JsonWriter::JsonScope s(w, "scene", JsonWriter::CompoundType::Object);
  {
    unordered_map<string, int> nodeIdx;

    auto& fnAddElem = [&nodeIdx, w, &allObjects](const char* base, ImBaseObject* obj) {
      char name[32];
      sprintf(name, "%s%.5d", base, ++nodeIdx[base]);
      _objectToNodeName[obj] = name;
      allObjects.push_back(obj);
    };

    for (ImBaseObject* obj : instance.scene.nullObjects)
      fnAddElem("Null", obj);

    for (ImBaseObject* obj : instance.scene.cameras)
      fnAddElem("Camera", obj);

    for (ImBaseObject* obj : instance.scene.lights)
      fnAddElem("Light", obj);

    for (ImBaseObject* obj : instance.scene.meshes)
      fnAddElem("Mesh", obj);
  }

  {
    JsonWriter::JsonScope s(w, "nodes", JsonWriter::CompoundType::Object);
    for (ImBaseObject* obj : allObjects)
    {
      JsonWriter::JsonScope s(w, _objectToNodeName[obj], JsonWriter::CompoundType::Object);
      vector<string> children;
      for (ImBaseObject* obj : obj->children)
        children.push_back(obj->name);
      w->EmitArray("children", children);
    }
  }

  w->Emit("buffer", instance.options.outputBase + ".dat");
  {
    JsonWriter::JsonScope s(w, "geometry", JsonWriter::CompoundType::Object);
    ExportWorldGeometry(w);
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportMaterialComponentShader(const ImMaterialComponent& component, JsonWriter* w)
{
  melange::BaseShader* shader = component.shader;

  int shaderType = shader->GetType();
  if (shaderType == Xbitmap)
  {
    JsonWriter::JsonScope s(w, "bitmap", JsonWriter::CompoundType::Object);

    melange::GeData data;
    shader->GetParameter(melange::BITMAPSHADER_FILENAME, data);
    if (data.GetType() == melange::DA_FILENAME)
    {
      w->Emit("filename", CopyString(data.GetFilename().GetString()));
    }
  }
  else if (shaderType == Xgradient)
  {
    JsonWriter::JsonScope s(w, "gradient", JsonWriter::CompoundType::Object);

    melange::GeData data;
    shader->GetParameter(melange::SLA_GRADIENT_GRADIENT, data);
    melange::Gradient* gradient = (melange::Gradient*)data.GetCustomDataType(CUSTOMDATATYPE_GRADIENT);

    enum GradientType
    {
      SLA_GRADIENT_TYPE_2D_U = 2000,
      SLA_GRADIENT_TYPE_2D_V,
      SLA_GRADIENT_TYPE_2D_DIAG,
      SLA_GRADIENT_TYPE_2D_RAD,
      SLA_GRADIENT_TYPE_2D_CIRC,
      SLA_GRADIENT_TYPE_2D_BOX,
      SLA_GRADIENT_TYPE_2D_STAR,
      SLA_GRADIENT_TYPE_2D_FOUR_CORNER,
      SLA_GRADIENT_TYPE_3D_LINEAR,
      SLA_GRADIENT_TYPE_3D_CYLINDRICAL,
      SLA_GRADIENT_TYPE_3D_SPHERICAL,
    };

    enum Interpolation
    {
      GRADIENT_INTERPOLATION_CUBICKNOT = 0,	///< Cubic knot.
      GRADIENT_INTERPOLATION_CUBICBIAS = 1,	///< Cubic bias.
      GRADIENT_INTERPOLATION_SMOOTHKNOT = 2,	///< Smooth knot.
      GRADIENT_INTERPOLATION_LINEARKNOT = 3,	///< Linear knot.
      GRADIENT_INTERPOLATION_LINEAR = 4,	///< Linear.
      GRADIENT_INTERPOLATION_NONE = 5,	///< None.
      GRADIENT_INTERPOLATION_EXP_UP = 6,	///< Exponential up.
      GRADIENT_INTERPOLATION_EXP_DOWN = 7		///< Exponential down.
    };

    string strType, strInterpolation;
    static const unordered_map<GradientType, string> typeToName = {
        {SLA_GRADIENT_TYPE_2D_V, "v"},
        {SLA_GRADIENT_TYPE_2D_U, "u"},
        {SLA_GRADIENT_TYPE_2D_DIAG, "diag"},
        {SLA_GRADIENT_TYPE_2D_RAD, "rad"},
        {SLA_GRADIENT_TYPE_2D_CIRC, "circ"},
    };

    static const unordered_map<Interpolation, string> interToName = {
      { GRADIENT_INTERPOLATION_CUBICKNOT, "cubic" },
      { GRADIENT_INTERPOLATION_CUBICBIAS, "cubic" },
      { GRADIENT_INTERPOLATION_SMOOTHKNOT, "smooth" },
      { GRADIENT_INTERPOLATION_LINEAR, "linear" },
    };

    GradientType gradientType = (GradientType)GetInt32Param(shader, melange::SLA_GRADIENT_TYPE);
    auto itType = typeToName.find(gradientType);
    if (itType == typeToName.end())
    {
      instance.Log(1, "Unsupported gradient type: %d\n", gradientType);
      return;
    }

    Interpolation inter = (Interpolation)GetInt32Data(gradient, GRADIENT_INTERPOLATION);
    auto itInter = interToName.find(inter);
    if (itInter == interToName.end())
    {
      instance.Log(1, "Unsupported interpolation type: %d\n", inter);
      return;
    }

    w->Emit("type", itType->second);
    w->Emit("interpolation", itInter->second);

    // silly hack because melange defines its own melange::swap..
    struct KnotProxy
    {
      melange::GradientKnot _;
    };

    deque<KnotProxy> knots;
    for (melange::Int32 k = 0; k < gradient->GetKnotCount(); k++)
    {
      knots.push_back(KnotProxy{ gradient->GetKnot(k) });
    }

    sort(knots.begin(), knots.end(), [](const KnotProxy& lhs, const KnotProxy& rhs) {
      return lhs._.pos < rhs._.pos;
    });

    // if the first or last keys aren't at 0 and 1, insert dummy keys just to make our lives easier..
    if (knots.front()._.pos > 0)
    {
      KnotProxy p = knots.front();
      p._.pos = 0;
      knots.push_front(p);
    }

    if (knots.back()._.pos < 1)
    {
      KnotProxy p = knots.back();
      p._.pos = 1;
      knots.push_back(p);
    }

    {
      JsonWriter::JsonScope s(w, "knots", JsonWriter::CompoundType::Array);
      for (const auto& kn : knots)
      {
        JsonWriter::JsonScope s(w, JsonWriter::CompoundType::Object);
        w->Emit("pos", kn._.pos);
        w->Emit("bias", kn._.bias);
        w->EmitArray("col", { kn._.col.x, kn._.col.y, kn._.col.z});
      }
    }
  }
  else
  {
    instance.Log(1, "Skipping unknown shader type: %d\n", shaderType);
  }
}

//------------------------------------------------------------------------------
void JsonExporter::ExportMaterials(const vector<ImMaterial*>& materials, JsonWriter* w)
{
  JsonWriter::JsonScope s(w, "materials", JsonWriter::CompoundType::Object);

  for (const ImMaterial* material : materials)
  {
    JsonWriter::JsonScope s(w, material->name, JsonWriter::CompoundType::Object);

    w->Emit("name", material->name);
    w->Emit("id", material->id);

    JsonWriter::JsonScope s2(w, "components", JsonWriter::CompoundType::Object);

    for (const ImMaterialComponent& comp : material->components)
    {
      JsonWriter::JsonScope s(w, comp.name, JsonWriter::CompoundType::Object);
      w->EmitArray("color", {comp.color.r, comp.color.g, comp.color.b});
      w->Emit("brightness", comp.brightness);

      if (comp.shader)
      {
        ExportMaterialComponentShader(comp, w);
      }
    }
  }
}


//------------------------------------------------------------------------------
bool JsonExporter::Export(SceneStats* stats)
{
  JsonWriter w;
  {
    JsonWriter::JsonScope s(&w, JsonWriter::CompoundType::Object);

    ExportSceneInfo(&w);

    ExportNullObjects(instance.scene.nullObjects, &w);
    ExportCameras(instance.scene.cameras, &w);
    ExportLights(instance.scene.lights, &w);
    ExportMeshes(instance.scene.meshes, &w);
    ExportMaterials(instance.scene.materials, &w);
    ExportPrimitives(instance.scene.primitives, &w);

    if (instance.options.sdf)
    {
      //CreateSDF(scene, options, &w);
      //CreateSDF2(scene, options, &w);
      CreateSDF3(instance, &w);
    }
  }

  // save the json file
  FILE* f = fopen(string(instance.options.outputPrefix + ".json").c_str(), "wt");
  if (f)
  {
    fputs(w.res.c_str(), f);
    fclose(f);
  }

  // save the data buffer
  if (buffer.size() > 0)
  {
    f = fopen(string(instance.options.outputPrefix + ".dat").c_str(), "wb");
    if (f)
    {
      fwrite(buffer.data(), buffer.size(), 1, f);
      fclose(f);
    }
  }

  return true;
}
