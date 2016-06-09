#include "json_exporter.hpp"


struct JsonWriter
{
  enum class CompoundType
  {
    Array,
    Object,
  };

  string Indent()
  {
    return string(indent, ' ');
  }

  void Begin(CompoundType type)
  {
    if (type == CompoundType::Array)
    {
      indent += 2;
      res += "[\n" + Indent();
    }
    else if (type == CompoundType::Object)
    {
      indent += 2;
      res += "{\n" + Indent();
    }

    _elemStack.push_back(type);
  }

  void End()
  {
    CompoundType type = _elemStack.back();
    _elemStack.pop_back();

    if (type == CompoundType::Array)
    {
      indent -= 2;
      res += "\n" + Indent() + "]";
    }
    else if (type == CompoundType::Object)
    {
      indent -= 2;
      res += "\n" + Indent() + "}";
    }
  }

  void EmitKey(const string& name)
  {
    res += Indent() + "\"" + name + "\": ";
  }

  void EmitKey(const char* name)
  {
    EmitKey(string(name));
  }

  void Emit()
  {
    res += "null";
  }

  //void Emit(bool value)
  //{
  //  res += value ? "true" : "false";
  //}

  void Emit(int value)
  {
    char buf[32];
    sprintf(buf, "%d", value);
    res += buf;
  }

  void Emit(double value)
  {
    char buf[32];
    sprintf(buf, "%lf", value);
    res += buf;
  }

  void Emit(const string& value)
  {
    res += value;
  }

  string res;

  deque<CompoundType> _elemStack;
  int indent = 0;
};


bool ExportAsJson(const ImScene& scene, const Options& options, SceneStats* stats)
{
  JsonWriter w;
  w.Begin(JsonWriter::CompoundType::Object);

  w.EmitKey("accessors ");
  w.Begin(JsonWriter::CompoundType::Object);
  w.EmitKey("accessor_21 ");
  w.Begin(JsonWriter::CompoundType::Object);

  w.EmitKey("bufferView");
  w.Emit("bufferView_29");

  w.End();
  w.End();

  w.End();
  return true;
}