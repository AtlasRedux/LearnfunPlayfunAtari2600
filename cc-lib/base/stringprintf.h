// Copyright 2002 and onwards Google Inc. and Tom 7
//
// Printf variants that place their output in a C++ string.
//
// Usage:
//      string result = StringPrintf("%d %s\n", 10, "hello");
//      SStringPrintf(&result, "%d %s\n", 10, "hello");
//      StringAppendF(&result, "%d %s\n", 20, "there");
//
// If in C++20, this also includes some wrappers for making
// std::format more palatable (to me).

#ifndef _CC_LIB_BASE_STRINGPRINTF_H
#define _CC_LIB_BASE_STRINGPRINTF_H

#include <stdarg.h>
#include <string>
#if __cplusplus >= 202002L
#include <iterator>
#include <format>
#endif

#include "base/macros.h"

// Return a C++ string.
extern std::string StringPrintf(const char *format, ...)
    // Tell the compiler to do printf format string checking.
    PRINTF_ATTRIBUTE(1,2);

// Store result into a supplied string and return it.
extern const std::string &SStringPrintf(std::string *dst,
                                        const char *format, ...)
    // Tell the compiler to do printf format string checking.
    PRINTF_ATTRIBUTE(2,3);

// Append result to a supplied string.
extern void StringAppendF(std::string *dst, const char *format, ...)
    // Tell the compiler to do printf format string checking.
    PRINTF_ATTRIBUTE(2,3);

// Lower-level routine that takes a va_list and appends to a specified
// string.  All other routines are just convenience wrappers around it.
extern void StringAppendV(std::string *dst, const char *format, va_list ap);

#if __cplusplus >= 202002L
template<class ...Args>
inline void AppendFormat(std::string *dst,
                         std::format_string<Args...> fmt,
                         Args &&...args) {
  (void)std::format_to(std::back_inserter(*dst), fmt,
                       std::forward<Args>(args)...);
}
#endif

#endif
