#ifndef PeriodicCaller_H
#define PeriodicCaller_H

#include <utility>

template <typename Func, typename... Args>
void CallEveryNFrames(int n, Func&& f, Args&&... args) {
  static int counter = 0;
  ++counter;
  if (counter == n) {
    counter = 0;
    std::forward<Func>(f)(std::forward<Args>(args)...);
  }
}

#endif  // PeriodicCaller_H
