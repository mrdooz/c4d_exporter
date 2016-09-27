#pragma once

//------------------------------------------------------------------------------
inline u32 SetBit(u32 value, int bit_num) { return (value | 1 << bit_num); }
inline bool IsBitSet(u32 value, int bit_num) { return !!(value & (1 << bit_num)); }
inline u32 ClearBit(u32 value, int bit_num) { return value & ~(1 << bit_num); }

//------------------------------------------------------------------------------
inline int BitsRequired(u32 input)
{
  u32 res = 1;
  while (input > (1U << res) - 1)
    ++res;
  return res;
}

//------------------------------------------------------------------------------
inline int CompressValue(int value, int bits)
{
  assert(BitsRequired(value) + 1 <= bits);
  if (value < 0)
    value = SetBit(-value, bits - 1);
  return value;
}

//------------------------------------------------------------------------------
inline int ExpandValue(u32 value, int bits)
{
  return (IsBitSet(value, bits - 1) ? -1 : 1) * ClearBit(value, bits - 1);
}

//------------------------------------------------------------------------------
inline u32 ZigZagEncode(int n)
{
  // https://developers.google.com/protocol-buffers/docs/encoding
  return (n << 1) ^ (n >> 31);
}

inline int ZigZagDecode(int n) { return (n >> 1) ^ (-(n & 1)); }

//------------------------------------------------------------------------------
class BitReader
{
public:
  BitReader(const u8* data, u32 len_in_bits);
  u32 Read(u32 count);
  u32 ReadVariant();
  bool Eof() const;

private:
  u32 _length_in_bits;
  u32 _bit_offset;
  u32 _byte_offset;
  const u8* _data;
};

//------------------------------------------------------------------------------
class BitWriter
{
public:
  BitWriter(u32 buf_size = 1024);
  ~BitWriter();
  void Write(u32 value, u32 num_bits);
  void WriteVariant(u32 value);
  void CopyOut(vector<u8>* out, u32* bit_length = nullptr, bool append = false);
  void CopyOut(u8** out, u32* bit_length);
  u64 GetSize() { return _byte_offset * 8 + _bit_offset; }

private:
  u32 _bit_offset;
  u32 _byte_offset;
  u32 _buf_size;
  u8* _buf;
};

//------------------------------------------------------------------------------
template <int NumBits>
struct BitSet
{
  BitSet() {
    memset(bits, 0, sizeof(bits));
  }

  void Set(int bit)
  {
    assert(bit < NumBits);
    int idx = bit / 8;
    int ofs = bit & 7;
    bits[idx] |= 1 << ofs;
  }

  void Clear(int bit)
  {
    int idx = bit / 8;
    int ofs = bit & 7;
    bits[idx] &= ~(1 << ofs);
  }

  bool IsSet(int bit)
  {
    int idx = bit / 8;
    int ofs = bit & 7;
    int mask = 1 << ofs;

    return (bits[idx] & mask) == mask;
  }

  bool ReadAndReset(int bit)
  {
    bool res = IsSet(bit);
    Clear(bit);
    return res;
  }

  u8 bits[(NumBits + 7) / 8];
};
