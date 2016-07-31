#pragma once

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

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

using namespace std;

#define RANGE(c) (c).begin(), (c).end()
#define WITH_SCENE_1 1
