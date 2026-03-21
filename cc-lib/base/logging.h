// Copyright 2010 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _CC_LIB_BASE_LOGGING_H
#define _CC_LIB_BASE_LOGGING_H

#include <cstdlib>
#include <iostream>
#include <stdlib.h>
#include <utility>
#include <version>

// Always-on checking
#define CHECK(x)        if (x) {} else LogMessageFatal(__FILE__, __LINE__).stream() << "Check failed: " #x "\n"
#define CHECK_LT(x, y)  CHECK((x) < (y))
#define CHECK_GT(x, y)  CHECK((x) > (y))
#define CHECK_LE(x, y)  CHECK((x) <= (y))
#define CHECK_GE(x, y)  CHECK((x) >= (y))
#define CHECK_EQ(x, y)  CHECK((x) == (y))
#define CHECK_NE(x, y)  CHECK((x) != (y))
#define CHECK_NOTNULL(x) CHECK((x) != nullptr)

#ifndef NDEBUG
// Debug-only checking.
#define DCHECK(condition) CHECK(condition)
#define DCHECK_EQ(val1, val2) CHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2) CHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2) CHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2) CHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2) CHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2) CHECK_GT(val1, val2)
#else
// Disabled unless we're in debug mode, but we still need
// something like DCHECK(cond) << "Error!" to compile.
#define DCHECK(condition) if (true) {} else LogMessageFatal(__FILE__, __LINE__).stream()
#define DCHECK_EQ(val1, val2) DCHECK(true)
#define DCHECK_NE(val1, val2) DCHECK(true)
#define DCHECK_LE(val1, val2) DCHECK(true)
#define DCHECK_LT(val1, val2) DCHECK(true)
#define DCHECK_GE(val1, val2) DCHECK(true)
#define DCHECK_GT(val1, val2) DCHECK(true)
#endif

#define LOG_INFO LogMessage(__FILE__, __LINE__)
#define LOG_ERROR LOG_INFO
#define LOG_WARNING LOG_INFO
#define LOG_FATAL LogMessageFatal(__FILE__, __LINE__)
#define LOG_QFATAL LOG_FATAL

#define VLOG(x) if((x)>0){} else LOG_INFO.stream()

#ifdef NDEBUG
#define DEBUG_MODE false
#define LOG_DFATAL LOG_ERROR
#else
#define DEBUG_MODE true
#define LOG_DFATAL LOG_FATAL
#endif

#define LOG(severity) LOG_ ## severity.stream()
#define LG LOG_INFO.stream()

namespace google_base {
class DateLogger {
 public:
  DateLogger();
  const char* HumanDate();
 private:
#ifndef __MINGW32__
  char buffer_[9] = {'\0'};
#endif
};
}  // namespace google_base

class LogMessage {
 public:
  LogMessage(const char* file, int line) {
    std::cerr << "[" << pretty_date_.HumanDate() << "] "
              << file << ":" << line << ": ";
  }
  ~LogMessage() { std::cerr << "\n"; }
  std::ostream& stream() { return std::cerr; }

 private:
  google_base::DateLogger pretty_date_;
  LogMessage(const LogMessage &) = delete;
  LogMessage &operator =(const LogMessage &) = delete;
};

class LogMessageFatal : public LogMessage {
 public:
  LogMessageFatal(const char* file, int line)
    : LogMessage(file, line) { }
  [[noreturn]]
  ~LogMessageFatal() {
    std::cerr << "\n" << std::flush;
    abort();
    #if __cpp_lib_unreachable
    std::unreachable();
    #endif
    #ifdef __GCC__
    __builtin_unreachable();
    #endif
  }
 private:
  LogMessageFatal(const LogMessageFatal &) = delete;
  LogMessageFatal &operator =(const LogMessageFatal &) = delete;
};

#endif  // BASE_LOGGING_H
