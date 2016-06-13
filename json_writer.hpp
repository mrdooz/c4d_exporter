#pragma once

struct JsonWriter
{
  enum class CompoundType
  {
    Array,
    Object,
  };

  struct JsonScope
  {
    JsonScope(JsonWriter* w, const string& key, CompoundType type) : w(w)
    {
      w->EmitKey(key);
      w->Begin(type);
    }

    JsonScope(JsonWriter* w, CompoundType type) : w(w)
    {
      w->Begin(type);
    }

    ~JsonScope()
    {
      w->End();
    }

    JsonWriter* w;
  };

  void Begin(CompoundType type)
  {
    res += type == CompoundType::Array ? "[" : "{";
    _indent += _indentSize;
    _elemStack.push_back(type);
    _itemCount.push_back(0);
  }

  void End()
  {
    CompoundType type = _elemStack.back();
    int count = _itemCount.back();
    _elemStack.pop_back();
    _itemCount.pop_back();

    _indent -= _indentSize;
    if (count > 0)
      res += "\n" + Indent();
    res += type == CompoundType::Array ? "]" : "}";
  }

  void EmitKey(const string& name)
  {
    MaybeAddDelimiter();
    res += Indent() + "\"" + name + "\": ";
  }

  void Emit()
  {
    res += "null";
  }

  void Emit(u32 value)
  {
    char buf[32];
    sprintf(buf, "%d", value);
    res += buf;
  }

  void Emit(int value)
  {
    char buf[32];
    sprintf(buf, "%d", value);
    res += buf;
  }

  void Emit(int64_t value)
  {
    char buf[32];
    sprintf(buf, "%ld", value);
    res += buf;
  }

  void Emit(uint64_t value)
  {
    char buf[32];
    sprintf(buf, "%ld", value);
    res += buf;
  }

  void Emit(double value)
  {
    char buf[32];
    sprintf(buf, "%lf", value);
    res += buf;
  }

  void Emit(const char* value)
  {
    res += "\"" + string(value) + "\"";
  }

  void Emit(const string& str)
  {
    Emit(str.c_str());
  }

  void Emit(bool value)
  {
    res += value ? "true" : "false";
  }

  template <typename T>
  void Emit(const string& key, T value)
  {
    EmitKey(key);
    Emit(value);
  }

  template <typename T>
  void EmitArray(const string& key, const vector<T>& elems)
  {
    EmitKey(key);
    Begin(CompoundType::Array);
    for (const T& t : elems)
    {
      EmitArrayElem(t);
    }
    End();
  }

  template <typename T>
  void EmitArray(const string& key, const initializer_list<T>& elems)
  {
    return EmitArray(key, vector<T>(elems));
  }

  template <typename T>
  void EmitObject(const string& key, const map<string, T>& obj)
  {
    EmitKey(key);
    Begin(CompoundType::Object);
    for (const auto& kv : obj)
    {
      Emit(kv.first, kv.second);
    }
    End();
  }

  template <typename T>
  void EmitArrayElem(T value)
  {
    MaybeAddDelimiter();
    res += Indent();
    Emit(value);
  }

  string Indent()
  {
    return string(_indent, ' ');
  }

  void MaybeAddDelimiter()
  {
    // check if we should add a ',' before the new line
    res += _itemCount.back() > 0 ? ",\n" : "\n";
    _itemCount[_itemCount.size() - 1]++;
  }

  string res;

  deque<CompoundType> _elemStack;
  deque<int> _itemCount;
  int _indent = 0;
  int _indentSize = 4;
};
