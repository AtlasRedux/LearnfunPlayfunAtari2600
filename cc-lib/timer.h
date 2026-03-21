
#ifndef _CC_LIB_TIMER_H
#define _CC_LIB_TIMER_H

#include <chrono>

// Simple wrapper around std::chrono::steady_clock.
// Starts timing when constructed. TODO: Pause() etc.
struct Timer {
  Timer() : starttime(std::chrono::steady_clock::now()) {}

  double Seconds() const {
    const std::chrono::time_point<std::chrono::steady_clock> stoptime =
      std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = stoptime - starttime;
    return elapsed.count();
  }

  double MS() const {
    return Seconds() * 1000.0;
  }

  void Reset() {
    starttime = std::chrono::steady_clock::now();
  }

 private:
  // morally const, but not const to allow assignment from other Timer
  // objects.
  std::chrono::time_point<std::chrono::steady_clock> starttime;
};

#endif
