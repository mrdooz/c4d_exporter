#pragma once

#define WITH_EMBREE 0

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#include <vector>
#include <deque>
#include <unordered_map>
#include <set>
#include <map>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <iterator>
#include <memory>

#include <c4d_file.h>
#include <c4d_ccurve.h>
#include <c4d_ctrack.h>

#define NOMINMAX
#include <windows.h>

#include <xmmintrin.h>
#include <pmmintrin.h>

#if WITH_EMBREE
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>
#pragma comment(lib: "embree.lib")
#endif`

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

using std::vector;
using std::string;
using std::wstring;
using std::pair;
using std::make_pair;
using std::hash;
using std::min;
using std::max;
using std::unordered_map;
using std::unordered_set;
using std::function;
using std::initializer_list;
using std::map;
using std::deque;
using std::shared_ptr;
using std::unique_ptr;
using std::make_unique;
