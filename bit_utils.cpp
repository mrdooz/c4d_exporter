#include "bit_utils.hpp"

BitReader::BitReader(const u8* data, u32 len_in_bits)
    : _length_in_bits(len_in_bits), _bit_offset(0), _byte_offset(0), _data(data)
{
}

u32 BitReader::ReadVariant()
{
  u32 res = 0;
  u32 ofs = 0;
  while (true)
  {
    u32 next = Read(8);
    res |= ((next & 0x7f) << ofs);
    if (!(next & 0x80))
      return res;
    ofs += 7;
  }
}

u32 BitReader::Read(u32 count)
{
  // clang-format off
  static const u32 read_mask[] =
  {
    0x00000000, 0x00000001, 0x00000003, 0x00000007, 0x0000000f, 0x0000001f, 0x0000003f, 0x0000007f, 0x000000ff,
    0x000001ff, 0x000003ff, 0x000007ff, 0x00000fff, 0x00001fff, 0x00003fff, 0x00007fff, 0x0000ffff, 0x0001ffff,
    0x0003ffff, 0x0007ffff, 0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff, 0x00ffffff, 0x01ffffff, 0x03ffffff,
    0x07ffffff, 0x0fffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff, 0xffffffff
  };
  // clang-format on

  // The 32 bit version is faster than the 64-bit version above..
  const u32* bb = (u32*)&_data[_byte_offset];
  // Read the coming 2 32 bit values
  const u32 a = bb[0];
  const u32 b = bb[1];

  // Calc new bit/byte offset
  const u32 bit_ofs = _bit_offset;
  _bit_offset = (bit_ofs + count) % 8;
  _byte_offset += (bit_ofs + count) / 8;

  const u32 num_lower_bits = 32 - bit_ofs;
  if (count > num_lower_bits)
  {
    const u32 num_higher_bits = count - num_lower_bits;
    // Note, we don't need to mask out the lower bits if we know that we're reading
    // the full length
    return (a >> bit_ofs) | ((b & read_mask[num_higher_bits]) << num_lower_bits);
  }
  else
  {
    return (a >> bit_ofs) & read_mask[count];
  }
}

bool BitReader::Eof() const
{
  return _byte_offset * 8 + _bit_offset >= _length_in_bits;
}

BitWriter::BitWriter(u32 buf_size)
    : _bit_offset(0), _byte_offset(0), _buf_size(buf_size), _buf((u8*)malloc(_buf_size))
{
}

BitWriter::~BitWriter()
{
  delete _buf;
}

void BitWriter::CopyOut(u8** out, u32* bit_length)
{
  *out = _buf;
  if (bit_length)
    *bit_length = _byte_offset * 8 + _bit_offset;
}

void BitWriter::CopyOut(vector<u8>* out, u32* bit_length, bool append)
{
  size_t len = _byte_offset + (_bit_offset + 7) / 8;
  size_t oldSize = out->size();
  out->resize(append ? len + oldSize : len);
  memcpy(append ? out->data() + oldSize : out->data(), _buf, len);
  if (bit_length)
    *bit_length = _byte_offset * 8 + _bit_offset;
}

void BitWriter::WriteVariant(u32 value)
{
  // based on https://developers.google.com/protocol-buffers/docs/encoding

  if (value < 1 << 7)
  {
    Write(value, 8);
  }
  else if (value < 1 << 14)
  {
    const u32 v0 = value & 0x7f;
    const u32 v1 = (value >> 7) & 0x7f;
    Write((v0 | 0x80) | (v1 << 8), 16);
  }
  else if (value < 1 << 21)
  {
    const u32 v0 = value & 0x7f;
    const u32 v1 = (value >> 7) & 0x7f;
    const u32 v2 = (value >> 14) & 0x7f;
    Write((v0 | 0x80) | ((v1 | 0x80) << 8) | (v2 << 16), 24);
  }
  else if (value < 1 << 28)
  {
    const u32 v0 = value & 0x7f;
    const u32 v1 = (value >> 7) & 0x7f;
    const u32 v2 = (value >> 14) & 0x7f;
    const u32 v3 = (value >> 21) & 0x7f;
    Write((v0 | 0x80) | ((v1 | 0x80) << 8) | ((v2 | 0x80) << 16) | (v3 << 24), 32);
  }
  else
  {
    const u32 v0 = value & 0x7f;
    const u32 v1 = (value >> 7) & 0x7f;
    const u32 v2 = (value >> 14) & 0x7f;
    const u32 v3 = (value >> 21) & 0x7f;
    const u32 v4 = (value >> 28) & 0x7f;
    Write((v0 | 0x80) | ((v1 | 0x80) << 8) | ((v2 | 0x80) << 16) | ((v3 | 0x80) << 24), 32);
    Write(v4, 8);
  }
}

void BitWriter::Write(u32 value, u32 count)
{

  static const u64 read_mask[] = {0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff};
  // Check if we need to resize (we need at least 64 bits due
  // to the 64 bit write)
  if (_byte_offset + 8 >= _buf_size)
  {
    u8* new_buf = (u8*)malloc(2 * _buf_size);
    memcpy(new_buf, _buf, _buf_size);
    free(_buf);
    _buf = new_buf;
    _buf_size *= 2;
  }

  u64* buf = (u64*)&_buf[_byte_offset];
  const u32 bit_ofs = _bit_offset;
  _byte_offset += (bit_ofs + count) / 8;
  _bit_offset = (bit_ofs + count) % 8;

  // Because we're working on LE, we left shift to get zeros in the
  // bits that are already used.
  const u64 prefix = (u64)value << bit_ofs;
  buf[0] = (buf[0] & read_mask[bit_ofs]) | prefix;
}
