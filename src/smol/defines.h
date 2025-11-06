#pragma once

#include <math.h>
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

typedef int8_t i8_t;
typedef int16_t i16_t;
typedef int32_t i32_t;
typedef int64_t i64_t;

typedef float f32;
typedef double f64;

static_assert(sizeof(u8_t) == 1, "STATIC ASSERT FAILED: u8_t not 1 byte wide");
static_assert(sizeof(u16_t) == 2, "STATIC ASSERT FAILED: u16_t not 2 byte wide");
static_assert(sizeof(u32_t) == 4, "STATIC ASSERT FAILED: u32_t not 4 byte wide");
static_assert(sizeof(u64_t) == 8, "STATIC ASSERT FAILED: u64_t not 8 byte wide");

static_assert(sizeof(i8_t) == 1, "STATIC ASSERT FAILED: i8_t not 1 byte wide");
static_assert(sizeof(i16_t) == 2, "STATIC ASSERT FAILED: i16_t not 2 byte wide");
static_assert(sizeof(i32_t) == 4, "STATIC ASSERT FAILED: i32_t not 4 byte wide");
static_assert(sizeof(i64_t) == 8, "STATIC ASSERT FAILED: i64_t not 8 byte wide");

enum smol_result_e
{
    SMOL_SUCCESS = 0,
    SMOL_FAILURE = -1
};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#    define SMOL_PLATFORM_WIN 1
#    ifndef _WIN64
#        error "Only 64-Bit Windows is supported."
#    endif

#elif defined(__linux__) || defined(__gnu_linux__)
#    define SMOL_PLATFORM_LINUX 1
#    if defined(__ANDROID__)
#        define SMOL_PLATFORM_ANDROID 1
#    endif

#elif defined(__unix__)
#    define SMOL_PLATFORM_UNIX 1

#elif defined(_POSIX_VERSION)
#    define SMOL_PLATFORM_POSIX 1

#elif __APPLE__
#    define SMOL_PLATFORM_APPLE 1
#    define SMOL_PLATFORM_MACOS 1 // no ios support, probably also no macos support
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#    if defined(SMOL_EXPORT)
#        define SMOL_API __declspec(dllexport)
#    else
#        define SMOL_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define SMOL_API __attribute__((visibility("default")))
#else
#    define SMOL_API
#endif

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif