
#ifndef _CC_LIB_BASE_PRINT_H
#define _CC_LIB_BASE_PRINT_H

#include <cstdio>
#include <format>

// Like std::print.
template<typename... Args>
inline void Print(std::format_string<Args...> fmt, Args &&...args) {
  auto str = std::format(fmt, std::forward<Args>(args)...);
  fwrite(str.data(), 1, str.size(), stdout);
}

template<typename... Args>
inline void Print(FILE *out, std::format_string<Args...> fmt, Args &&...args) {
  auto str = std::format(fmt, std::forward<Args>(args)...);
  fwrite(str.data(), 1, str.size(), out);
}

#endif
