#ifndef __TASBOT_PLATFORM_H
#define __TASBOT_PLATFORM_H

// Platform compatibility shim for MSVC vs POSIX.
// Include this instead of <unistd.h>, <sys/types.h>, <sys/time.h>, etc.

#ifdef _MSC_VER
  #include <io.h>
  #include <direct.h>
  #include <process.h>

  #ifndef access
  #define access _access
  #endif
  #ifndef getcwd
  #define getcwd _getcwd
  #endif
  #ifndef getpid
  #define getpid _getpid
  #endif

  // signal.h is available on MSVC
  #include <signal.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/time.h>
  #include <sys/stat.h>
  #include <signal.h>
  #include <strings.h>
#endif

#endif
