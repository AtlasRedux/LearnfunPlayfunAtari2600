// Black-box optimization. This takes an arbitrary function R^N -> R,
// bounds on the inputs, and tries to find an input in bounds that
// minimizes the return value.
//
// This is just a simple wrapper around "BiteOpt"; see here:
// https://github.com/avaneev/biteopt
//
// Consider optimizer.h for a fancier interface that allows integral
// arguments and termination conditions.
//
// TODO: For fancier uses, it would probably be better to just expose
// this as an "open loop", where it keeps some internal state (including
// the best, and maybe some stats about convergence) and proposes a
// next vector to try, and takes evidence of the form (vec, score) to
// update its state.

#ifndef _CC_LIB_OPT_H
#define _CC_LIB_OPT_H

#include <array>
#include <functional>
#include <span>
#include <utility>
#include <vector>

struct Opt {
  // Returns the parameter vector that produced the smallest
  // value, and the value of f on that vector.
  template<int N>
  static std::pair<std::array<double, N>, double>
  Minimize(const std::function<double(const std::array<double, N> &)> &f,
           // Bounds must be finite.
           const std::array<double, N> &lower_bound,
           const std::array<double, N> &upper_bound,
           // Approximately, the number of times to call f.
           int iters,
           int depth = 1,
           int attempts = 10,
           // Optimization is deterministic, but you can change the
           // pseudorandom seed if calling multiple times.
           int random_seed = 1);

  // As above, but with n as a runtime value.
  static std::pair<std::vector<double>, double>
  Minimize(int n,
           const std::function<double(std::span<const double>)> &f,
           const std::vector<double> &lower_bound,
           const std::vector<double> &upper_bound,
           int iters,
           int depth = 1,
           int attempts = 10,
           int random_seed = 1);

  // Convenience versions for small N.
  // Returns {best_arg, f(best_arg)}.
  inline static std::pair<double, double>
  Minimize1D(const std::function<double(double)> &f,
             double lower_bound,
             double upper_bound,
             int iters,
             int depth = 1,
             int attempts = 10,
             int random_seed = 1);

  inline static std::pair<std::tuple<double, double>, double>
  Minimize2D(const std::function<double(double, double)> &f,
             std::tuple<double, double> lower_bound,
             std::tuple<double, double> upper_bound,
             int iters,
             int depth = 1,
             int attempts = 10,
             int random_seed = 1);

  inline static std::pair<std::tuple<double, double, double>, double>
  Minimize3D(const std::function<double(double, double, double)> &f,
             std::tuple<double, double, double> lower_bound,
             std::tuple<double, double, double> upper_bound,
             int iters,
             int depth = 1,
             int attempts = 10,
             int random_seed = 1);

  inline static std::pair<std::tuple<double, double, double, double>, double>
  Minimize4D(const std::function<double(double, double, double, double)> &f,
             std::tuple<double, double, double, double> lower_bound,
             std::tuple<double, double, double, double> upper_bound,
             int iters,
             int depth = 1,
             int attempts = 10,
             int random_seed = 1);

  // TODO: 4, etc.

  // TODO: Improve the way we specify tuning parameters (they can
  // be exposed but it should be easy to ignore them, especially
  // their interaction with iters).

private:
  typedef double (*internal_func)(int N, const double* x,
                                  void* func_data);

  static void internal_minimize(
      int N, internal_func f, void* data,
      const double* lb, const double* ub, double* x, double* minf,
      int iter, int M = 1, int attc = 10,
      int random_seed = 1);
};


// Template implementations follow.

template<int N>
std::pair<std::array<double, N>, double>
Opt::Minimize(
    const std::function<double(const std::array<double, N> &)> &f,
    const std::array<double, N> &lower_bound,
    const std::array<double, N> &upper_bound,
    int iters,
    int depth,
    int attempts,
    int random_seed) {
  static_assert(N > 0);

  using Func = std::function<double(const std::array<double, N> &)>;

  auto wrap_f = [](int n_, const double *args, void* data) -> double {
      auto *f = (const Func *)data;
      std::array<double, N> in;
      for (int i = 0; i < N; i++) in[i] = args[i];
      return (*f)(in);
    };
  std::array<double, N> out;
  double out_v = 0.0;
  Opt::internal_minimize(N, +wrap_f, const_cast<Func*>(&f),
                         lower_bound.data(), upper_bound.data(),
                         out.data(), &out_v, iters, depth, attempts,
                         random_seed);
  return {out, out_v};
}

std::pair<double, double>
Opt::Minimize1D(const std::function<double(double)> &f,
                double lower_bound,
                double upper_bound,
                int iters,
                int depth,
                int attempts,
                int random_seed) {
  const auto [aarg, best] =
    Minimize<1>([&f](const std::array<double, 1> &d) -> double {
        return f(std::get<0>(d));
      },
    std::array<double, 1>{lower_bound},
    std::array<double, 1>{upper_bound},
    iters, depth, attempts, random_seed);
  return std::make_pair(std::get<0>(aarg), best);
}

std::pair<std::tuple<double, double>, double>
Opt::Minimize2D(const std::function<double(double, double)> &f,
                std::tuple<double, double> lower_bound,
                std::tuple<double, double> upper_bound,
                int iters,
                int depth,
                int attempts,
                int random_seed) {
  const auto [aarg, best] =
    Minimize<2>([&f](const std::array<double, 2> &d) -> double {
        return f(std::get<0>(d), std::get<1>(d));
      },
    std::array<double, 2>{std::get<0>(lower_bound), std::get<1>(lower_bound)},
    std::array<double, 2>{std::get<0>(upper_bound), std::get<1>(upper_bound)},
    iters, depth, attempts, random_seed);
  return std::make_pair(
      std::make_tuple(std::get<0>(aarg), std::get<1>(aarg)), best);
}

std::pair<std::tuple<double, double, double>, double>
Opt::Minimize3D(const std::function<double(double, double, double)> &f,
                std::tuple<double, double, double> lower_bound,
                std::tuple<double, double, double> upper_bound,
                int iters,
                int depth,
                int attempts,
                int random_seed) {
  const auto [aarg, best] =
    Minimize<3>([&f](const std::array<double, 3> &d) -> double {
        return f(std::get<0>(d), std::get<1>(d), std::get<2>(d));
      },
    std::array<double, 3>{
        std::get<0>(lower_bound),
        std::get<1>(lower_bound),
        std::get<2>(lower_bound)},
    std::array<double, 3>{
        std::get<0>(upper_bound),
        std::get<1>(upper_bound),
        std::get<2>(upper_bound)},
    iters, depth, attempts, random_seed);
  return std::make_pair(
      std::make_tuple(std::get<0>(aarg),
                      std::get<1>(aarg),
                      std::get<2>(aarg)), best);
}

std::pair<std::tuple<double, double, double, double>, double>
Opt::Minimize4D(const std::function<double(double, double, double, double)> &f,
                std::tuple<double, double, double, double> lower_bound,
                std::tuple<double, double, double, double> upper_bound,
                int iters,
                int depth,
                int attempts,
                int random_seed) {
  const auto [aarg, best] =
    Minimize<4>([&f](const std::array<double, 4> &d) -> double {
        return f(std::get<0>(d), std::get<1>(d),
                 std::get<2>(d), std::get<3>(d));
      },
    std::array<double, 4>{
        std::get<0>(lower_bound),
        std::get<1>(lower_bound),
        std::get<2>(lower_bound),
        std::get<3>(lower_bound)},
    std::array<double, 4>{
        std::get<0>(upper_bound),
        std::get<1>(upper_bound),
        std::get<2>(upper_bound),
        std::get<3>(upper_bound)},
    iters, depth, attempts, random_seed);
  return std::make_pair(
      std::make_tuple(std::get<0>(aarg),
                      std::get<1>(aarg),
                      std::get<2>(aarg),
                      std::get<3>(aarg)), best);
}


#if 0
// TODO: Should be possible to make a templated version that takes
// f(x, y, z, ...)! I can write down this function signature (see below)
// but I don't know any way to *call* the function with the correct number
// of arguments (in this case pulled from some pointer).

// Note: Caller probably has to either explicitly pass <double,
// double> as template args or manually construct the std::function,
// since with the current approach, it can't deduce what argument
// type we're asking to construct the function<> at. Might be possible
// to coax it with something like
// template <class T, class... Ts, class = std::enable_if_t<(std::is_same_v<T, Ts> && ...)>
// or maybe the homogeneous variadic functions proposal.

// Pass a function that directly takes some number of double
// arguments.  XXX docs
template<class ...Doubles>
static std::pair<std::array<double, sizeof...(Doubles)>, double>
MinimizeF(const std::function<double(Doubles...)> &f,
          std::array<double, sizeof...(Doubles)> lower_bound,
          std::array<double, sizeof...(Doubles)> upper_bound,
          int iters,
          int depth = 1,
          int attempts = 10);


template<class ...Doubles>
// std::pair<std::tuple<Doubles...>, double>
void
Opt::MinimizeF(const std::function<double(Doubles...)> &f,
               std::array<double, sizeof...(Doubles)> lower_bound,
               std::array<double, sizeof...(Doubles)> upper_bound,
               int iters,
               int depth,
               int attempts) {
  constexpr int N = sizeof...(Doubles);

  auto wrap_f = [](int n_, const double *args, void* data) -> double {
      auto *f = (std::function<double(Doubles...)> *)data;
      // XXX but how do I generate N args like args[0], args[1], ...?
      return (*f)(... ? ...);
    };

  std::array<double, N> out;
  double out_v = 0.0;
  Opt::internal_minimize(N, +wrap_f, &f,
                         lower_bound.data(), upper_bound.data(),
                         out.data(), &out_v, iters, depth, attempts);
  return {out, out_v};
}
#endif

#endif
