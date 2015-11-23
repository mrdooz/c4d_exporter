#pragma once
#include <stdint.h>
#include <vector>
#include <deque>
#include <unordered_map>

typedef uint32_t u32;
typedef uint8_t u8;

namespace boba
{
  using namespace std;

  class DeferredWriter
  {
  public:
    DeferredWriter();
    ~DeferredWriter();

    // When adding deferred data, f ex a vector, one of these structs is created, and binds
    // together the caller and the data
    struct DeferredData
    {
      DeferredData(u32 ref, const void *d, u32 len, bool saveBlobSize)
      : ref(ref)
      , saveBlobSize(saveBlobSize)
      {
        data.resize(len);
        memcpy(&data[0], d, len);
      }

      vector<u8> data;
      // The position in the file that references the deferred block
      u32 ref;
      bool saveBlobSize;
    };

    struct LocalFixup
    {
      LocalFixup(u32 ref, u32 dst)
      : ref(ref)
      , dst(dst)
      {
      }
      u32 ref;
      u32 dst;
    };

    bool Open(const char *filename);
    void Close();

    void WritePtr(intptr_t ptr);
    void WriteDeferredStart();
    u32 AddDeferredString(const string& str);
    u32 AddDeferredData(const void *data, u32 len, bool writeDataSize);
    u32 CreateFixup();
    void InsertFixup(u32 id);

    template<typename T>
    void AddDeferredVector(const vector<T>& v)
    {
      if (!v.empty())
      {
        u32 len = (u32)v.size() * sizeof(T);
        _deferredData.push_back(DeferredData(GetFilePos(), v.data(), len, false));
      }
      WritePtr(0);
    }

    void WriteDeferredData();

    template <class T>
    void Write(const T& data) const
    {
      fwrite(&data, 1, sizeof(T), _f);
    }

    void WriteRaw(const void *data, int len);

    void StartBlockMarker();
    void EndBlockMarker();

    int GetFilePos() const;
    void SetFilePos(int p);

  private:
    void PushFilePos();
    void PopFilePos();
    u32 DeferredDataSize() const;

    vector<LocalFixup> _localFixups;
    vector<DeferredData> _deferredData;
    deque<int> _filePosStack;
    // Where in the file should we write the location of the deferred data (this is not the location itself)
    u32 _deferredStartPos;
    FILE *_f;

    unordered_map<u32, u32> _pendingFixups;
    u32 _nextFixup = 0;

    deque<int> _blockStack;
  };
}
