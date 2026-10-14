#ifndef _N64_OS_TYPES_H_
#define _N64_OS_TYPES_H_

// Ultra64 type aliases for 3DS
#include <stdint.h>
#include <stddef.h>

typedef int8_t      s8;
typedef uint8_t     u8;
typedef int16_t     s16;
typedef uint16_t    u16;
typedef int32_t     s32;
typedef uint32_t    u32;
typedef int64_t     s64;
typedef uint64_t    u64;
typedef float       f32;
typedef double      f64;

// N64 size types
typedef u32         size_t_n64;

// Boolean type used in N64 code
typedef s32         Bool;

#endif
