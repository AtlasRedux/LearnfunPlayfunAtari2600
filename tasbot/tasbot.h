
#ifndef __TASBOT_H
#define __TASBOT_H

#include "../cc-lib/util.h"
#include "../cc-lib/heap.h"

#include "../cc-lib/base/stringprintf.h"

#define TASBOT_SAMPLE_RATE 44100

#define DEBUGGING 0

// TODO: Use good logging package.
#define CHECK(condition) \
  while (!(condition)) {                                    \
    fprintf(stderr, "%s:%d. Check failed: %s\n",            \
            __FILE__, __LINE__, #condition                  \
            );                                              \
    abort();                                                \
  }

#define DCHECK(condition) \
  while (DEBUGGING && !(condition)) {                       \
    fprintf(stderr, "%s:%d. DCheck failed: %s\n",           \
            __FILE__, __LINE__, #condition                  \
            );                                              \
    abort();                                                \
  }

#define NOT_COPYABLE(classname) \
  private: \
  classname(const classname &); \
  classname &operator =(const classname &)

using namespace std;

#endif
