/* Standalone integer type definitions for TASBot Atari.
   Replaces fceu/types.h — provides the same typedefs without
   any FCEUX-specific baggage. */

#ifndef __TASBOT_TYPES_H
#define __TASBOT_TYPES_H

#if defined(MSVC) || defined(_MSC_VER)
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef signed char int8;
typedef signed short int16;
typedef signed int int32;
typedef __int64 int64;
typedef unsigned __int64 uint64;
#else
#include <cstdint>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint64_t uint64;
#endif

#endif
