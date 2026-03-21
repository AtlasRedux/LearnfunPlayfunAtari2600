// Generate lines using some classic graphics algorithms.
// Templated on integer/float types since sometimes it is helpful
// to use lower-precision types in embedded applications.

#ifndef _CC_LIB_LINES_H
#define _CC_LIB_LINES_H

#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>
#include <cmath>
#include <type_traits>
#include <vector>
#include <optional>
#include <functional>

// Generate lines with Bresenham's algorithm. Use like this:
//
// /* Draw a line from (3, 4) to (-7, 8). */
// for (const std::pair<int, int> point : Line<int>{3, 4, -7, 8}) {
//   int x = point.first, y = point.second;
//   drawpixel(x, y);
// }
//
// C++17:
// for (auto [x, y] : Line<int>{3, 4, -7, 8})
//   drawpixel(x, y);
template<class Int>
class Line {
 public:
  static_assert(std::is_integral<Int>::value, "Line<T> requires integral T.");
  Line(Int x0, Int y0, Int x1, Int y1);
  static Line<Int> Empty();

  // This iterator is only designed for ranged-for loops; the operators
  // may have counter-intuitive behavior.
  struct iterator {
    std::pair<Int, Int> operator *() const { return {x, y}; }
    void operator ++();
    bool operator !=(const iterator &other) const;
   private:
    iterator(const Line &parent) : parent(parent) {}
    const Line &parent;
    Int x = 0, y = 0;
    Int frac = 0;
    friend class Line;
  };
  iterator begin() const;
  iterator end() const;

  // TODO: Fix this!
  #if 0
  // Inclusive clip rectangle.
  static Line<Int> ClippedLine(Int x0, Int y0, Int x1, Int y1,
                               Int xmin, Int ymin,
                               Int xmax, Int ymax);
  #endif

 private:
  Line(Int x0, Int y0, Int x1, Int y1,
       Int dx, Int dy, Int stepx, Int stepy,
       Int start_frac);
  // All members morally const.
  const Int x0, y0, x1, y1;
  Int dx, dy;
  Int stepx, stepy;
  Int start_frac;
};

// Anti-aliased line using Wu's algorithm. The pixel to plot is
// accompanied by a brightness fraction in [0, 1].
// This is not suited for an iterator-based interface since it treats
// the endpoints separately, and draws two pixels per iteration.
// Not possible to return early.
//
// /* Draw a line from (1.0, 3.1) to (-7, 8.5), using single-precision
//    floats (deduced from args) and int output. */
// LineAA::Draw<int>(1.0f, 3.1f, -7.0f, 8.5f,
//                   [](int x, int y, float f) {
//                       blendpixel(x, y, f * 255.0f);
//                   });
class LineAA {
public:
  template<class Int, class Float, class Fn>
  static void Draw(Float x0, Float y0, Float x1, Float y1, Fn drawpixel);
};


// Compute the point of intersection between two line segments
// (given as their endpoints), or return nullopt if they do
// not intersect.
//
// (Note that even for double inputs, this does some float
// calculations and returns float. TODO: Could make it
// use double (or long double) if inputs are that type, with
// significant added trickery.)
// Ported from sml-lib.
template<class Num = float>
inline std::optional<std::pair<float, float>> LineIntersection(
    // First segment
    Num p0x, Num p0y, Num p1x, Num p1y,
    // Second segment
    Num p2x, Num p2y, Num p3x, Num p3y);

// Return the closest point (to x,y) on the given line segment.
// It may be one of the endpoints.
inline std::pair<float, float>
ClosestPointOnSegment(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y);

// Return the minimum distance between the point and the line segment.
inline float PointLineDistance(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y);

// Same, but for a line that's known to be horizontal.
inline float PointHorizLineDistance(
    // Line segment
    float x0, float y0, float x1, /* y1 = y0 */
    // Point to test
    float x, float y);

// ... and vertical.
inline float PointVertLineDistance(
    // Line segment
    float x0, float y0, /* x1 = x0 */ float y1,
    // Point to test
    float x, float y);

template<class Num = float>
std::pair<Num, Num> ReflectPointAboutLine(
    // Line segment
    Num x0, Num y0,
    Num x1, Num y1,
    // Point to reflect
    Num x, Num y);

// Return a vector of endpoints, not including the start point (but
// including the end), to draw as individual line segments in order to
// approximate the given quadratic Bezier curve.
//
// Num should work as integral (then all math is integral) or
// floating-point types.
template<class Num = float>
inline std::vector<std::pair<Num, Num>> TesselateQuadraticBezier(
    // starting vertex
    Num x0, Num y0,
    // control point
    Num x1, Num y1,
    // end point
    Num x2, Num y2,
    Num max_error_squared = Num(2),
    int max_depth = 16);


template<class Num = float>
inline std::optional<std::tuple<Num, Num, Num, Num>>
ClipLineToRectangle(Num x0, Num y0, Num x1, Num y1,
                    Num xmin, Num ymin, Num xmax, Num ymax) {
  // This would compile with integers, but the integer division
  // is problematic, and the result would often not describe
  // the same line.
  static_assert(std::is_floating_point<Num>::value,
                "ClipLineToRectangle needs a floating-point "
                "template argument.");

  // via "Another Simple but Faster Method for 2D Line Clipping",
  // Matthes & Drakopoulos 2019
  if (x0 < xmin && x1 < xmin) return std::nullopt;
  if (x0 > xmax && x1 > xmax) return std::nullopt;
  if (y0 < ymin && y1 < ymin) return std::nullopt;
  if (y0 > ymax && y1 > ymax) return std::nullopt;

  Num x[2] = {x0, x1};
  Num y[2] = {y0, y1};

  for (int i = 0; i < 2; i++) {
    if (x[i] < xmin) {
      x[i] = xmin;
      y[i] = ((y1 - y0) / (x1 - x0)) * (xmin - x0) + y0;
    } else if (x[i] > xmax) {
      x[i] = xmax;
      y[i] = ((y1 - y0) / (x1 - x0)) * (xmax - x0) + y0;
    }

    if (y[i] < ymin) {
      y[i] = ymin;
      x[i] = ((x1 - x0) / (y1 - y0)) * (ymin - y0) + x0;
    } else if (y[i] > ymax) {
      y[i] = ymax;
      x[i] = ((x1 - x0) / (y1 - y0)) * (ymax - y0) + x0;
    }
  }

  if (!(x[0] < xmin && x[1] < xmin) && !(x[0] > xmax && x[1] > xmax)) {
    return std::make_tuple(x[0], y[0], x[1], y[1]);
  } else {
    return std::nullopt;
  }
}


// Template implementations follow.

template<class Int>
Line<Int>::Line(Int x0, Int y0, Int x1, Int y1) :
  x0(x0), y0(y0), x1(x1), y1(y1) {
  dy = y1 - y0;
  dx = x1 - x0;

  if (dy < 0) {
    dy = -dy;
    stepy = -1;
  } else {
    stepy = 1;
  }

  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  } else {
    stepx = 1;
  }

  dy <<= 1;
  dx <<= 1;

  if (dx > dy) {
    start_frac = dy - (dx >> 1);
  } else {
    start_frac = dx - (dy >> 1);
  }
}

template<class Int>
typename Line<Int>::iterator Line<Int>::begin() const {
  iterator it{*this};
  it.x = x0;
  it.y = y0;
  it.frac = start_frac;
  return it;
}

template<class Int>
typename Line<Int>::iterator Line<Int>::end() const {
  iterator it{*this};

  // One step beyond the end point, so that the line includes
  // (x1, y1).
  if (dx > dy) {
    it.x = x1 + stepx;
  } else {
    it.y = y1 + stepy;
  }
  return it;
}

template<class Int>
Line<Int>::Line(Int x0, Int y0, Int x1, Int y1,
                Int dx, Int dy, Int stepx, Int stepy,
                Int start_frac) : x0(x0), y0(y0),
                                  x1(x1), y1(y1),
                                  dx(dx), dy(dy),
                                  stepx(stepx), stepy(stepy),
                                  start_frac(start_frac) {
}

template<class Int>
Line<Int> Line<Int>::Empty() {
  // Any line such that .begin() == .end()
  Line<int> empty(
    // x0, y0, x1, y1 (we passed the end by one pixel)
    1, 1, 0, 0,
    // dx, dy, stepx, stepy
    1, 1, 1, 1,
    // start_frac
    0);

  assert(!(empty.begin() != empty.end()));
  return empty;
}

template<class Int>
bool Line<Int>::iterator::operator !=(const iterator &other) const {
  return parent.dx > parent.dy ?
    x != other.x :
    y != other.y;
}

template<class Int>
void Line<Int>::iterator::operator ++() {
  if (parent.dx > parent.dy) {
    if (frac >= 0) {
      y += parent.stepy;
      frac -= parent.dx;
    }
    x += parent.stepx;
    frac += parent.dy;
  } else {
    if (frac >= 0) {
      x += parent.stepx;
      frac -= parent.dy;
    }
    y += parent.stepy;
    frac += parent.dx;
  }
}

// FIXME: This is buggy :(
// Inclusive clip rectangle.
#if 0
template<class Int>
Line<Int>::Line<Int> Line<Int>::ClippedLine(Int x0, Int y0, Int x1, Int y1,
                                            Int clip_xmin, Int clip_ymin,
                                            Int clip_xmax, Int clip_ymax) {
  auto floor_div = [](int a, int b) {
      const int d = a / b;
      const int r = a % b;
      return r ? (d - ((a < 0) ^ (b < 0))) : d;
    };

  // Vertical line
  if (x0 == x1) {
    // TODO: Return empty line for cases where we miss the clip
    // window completely.
    if (x0 < clip_xmin || x0 > clip_xmax)
      return Empty();

    if (y0 <= y1) {
      if (y1 < clip_ymin || y0 > clip_ymax)
        return Empty();
      y0 = std::max(y0, clip_ymin);
      y1 = std::min(y1, clip_ymax);
      return Line(x0, y0, x1, y1);
    } else {
      if (y0 < clip_ymin || y1 > clip_ymax)
        return Empty();
      y1 = std::max(y1, clip_ymin);
      y0 = std::min(y0, clip_ymax);
      return Line(x0, y0, x1, y1);
    }
  }

  // Horizontal line
  if (y0 == y1) {
    if (y0 < clip_ymin || y0 > clip_ymax)
      return Empty();

    if (x0 <= x1) {
      if (x1 < clip_xmin || x0 > clip_xmax)
        return Empty();
      x0 = std::max(x0, clip_xmin);
      x1 = std::min(x1, clip_xmax);
      return Line(x0, y0, x1, y1);
    } else {
      if (x0 < clip_xmin || x1 > clip_xmax)
        return Empty();
      x1 = std::max(x1, clip_xmin);
      x0 = std::min(x0, clip_xmax);
      return Line(x0, y0, x1, y1);
    }
  }

  // General case. Flip signs as needed.
  int sign_x = 0, sign_y = 0;

  if (x0 < x1) {
    if (x0 > clip_xmax || x1 < clip_xmin)
      return Empty();
    sign_x = 1;
  } else {
    if (x1 > clip_xmax || x0 < clip_xmin)
      return Empty();
    sign_x = -1;
    x0 = -x0;
    x1 = -x1;
    clip_xmax = -clip_xmax;
    clip_xmin = -clip_xmin;
    std::swap(clip_xmax, clip_xmin);
  }

  if (y0 < y1) {
    if (y0 > clip_ymax || y1 < clip_ymin)
      return Empty();
    sign_y = 1;
  } else {
    if (y1 > clip_ymax || y0 < clip_ymin)
      return Empty();
    sign_y = -1;

    y0 = -y0;
    y1 = -y1;
    clip_ymax = -clip_ymax;
    clip_ymin = -clip_ymin;
    std::swap(clip_ymax, clip_ymin);
  }

  int delta_x = x1 - x0;
  int delta_y = y1 - y0;

  int delta_x_step = 2 * delta_x;
  int delta_y_step = 2 * delta_y;

  // Plotting values
  int x_pos = x0;
  int y_pos = y0;

  if (delta_x >= delta_y) {
    int error = delta_y_step - delta_x;
    bool set_exit = false;

    // Line starts below the clip window.
    if (y0 < clip_ymin) {
      int temp = (2 * (clip_ymin - y0) - 1) * delta_x;
      int msd = floor_div(temp, delta_y_step);
      x_pos += msd;

      // Line misses the clip window entirely.
      if (x_pos > clip_xmax)
        return Empty();

      // Line starts.
      if (x_pos >= clip_xmin) {
        int rem = temp - msd * delta_y_step;

        y_pos = clip_ymin;
        error -= rem + delta_x;

        if (rem > 0) {
          x_pos += 1;
          error += delta_y_step;
        }
        set_exit = true;
      }
    }

    // Line starts left of the clip window.
    if (!set_exit && x0 < clip_xmin) {
      int temp = delta_y_step * (clip_xmin - x0);
      int msd = floor_div(temp, delta_x_step);
      y_pos += msd;
      int rem = temp % delta_x_step;

      // Line misses clip window entirely.
      if (y_pos > clip_ymax || (y_pos == clip_ymax && rem >= delta_x)) {
        return Empty();
      }

      x_pos = clip_xmin;
      error += rem;

      if (rem >= delta_x) {
        y_pos += 1;
        error -= delta_x_step;
      }
    }

    int x_pos_end = x1;

    if (y1 > clip_ymax) {
      int temp = delta_x_step * (clip_ymax - y0) + delta_x;
      int msd = floor_div(temp, delta_y_step);
      x_pos_end = x0 + msd;

      if ((temp - msd * delta_y_step) == 0) {
        x_pos_end--;
      }
    }

    x_pos_end = std::min(x_pos_end, clip_xmax);

    if (sign_y == -1) {
      y_pos = -y_pos;
    }
    if (sign_x == -1) {
      x_pos = -x_pos;
      x_pos_end = -x_pos_end;
    }
    delta_x_step -= delta_y_step;

    // Now do loop.
    return Line(x_pos, y_pos, x_pos_end, 0,
                delta_x_step, delta_y_step, sign_x, sign_y,
                error);

   } else {
    // Line is steep '/' (delta_x < delta_y).
    // Same as previous block of code with swapped x/y axis.

    int error = delta_x_step - delta_y;
    bool set_exit = false;

    // Line starts left of the clip window.
    if (x0 < clip_xmin) {
      int temp = (2 * (clip_xmin - x0) - 1) * delta_y;
      int msd = floor_div(temp, delta_x_step);
      y_pos += msd;

        // Line misses the clip window entirely.
      if (y_pos > clip_ymax) {
        return Empty();
      }

      // Line starts.
      if (y_pos >= clip_ymin) {
        int rem = temp - msd * delta_x_step;

        x_pos = clip_xmin;
        error -= rem + delta_y;

        if (rem > 0) {
          y_pos += 1;
          error += delta_x_step;
        }
        set_exit = true;
      }
    }

    // Line starts below the clip window.
    if (!set_exit && y0 < clip_ymin) {
      int temp = delta_x_step * (clip_ymin - y0);
      int msd = floor_div(temp, delta_y_step);
      x_pos += msd;
      int rem = temp % delta_y_step;

      // Line misses clip window entirely.
      if (x_pos > clip_xmax || (x_pos == clip_xmax && rem >= delta_y)) {
        return Empty();
      }

      y_pos = clip_ymin;
      error += rem;

      if (rem >= delta_y) {
        x_pos++;
        error -= delta_y_step;
      }
    }

    int y_pos_end = y1;

    if (x1 > clip_xmax) {
      int temp = delta_y_step * (clip_xmax - x0) + delta_y;
      int msd = floor_div(temp, delta_x_step);
      y_pos_end = y0 + msd;

      if ((temp - msd * delta_x_step) == 0) {
        y_pos_end--;
      }
    }

    y_pos_end = std::min(y_pos_end, clip_ymax);

    if (sign_x == -1) {
      x_pos = -x_pos;
    }
    if (sign_y == -1) {
      y_pos = -y_pos;
      y_pos_end = -y_pos_end;
    }
    delta_y_step -= delta_x_step;

    return Line(x_pos, y_pos, 0, y_pos_end,
                delta_x_step, delta_y_step, sign_x, sign_y,
                error);
  }
}
#endif

// TODO: There may be some problem with the endpoint drawing;
// there seem to be discontinuities when drawing a polyline.
// (Could also be a problem with ImageRGBA::BlendPixel?)
template<class Int, class Float, class Fn>
void LineAA::Draw(Float x0, Float y0, Float x1, Float y1, Fn drawpixel) {
  static_assert(std::is_integral<Int>::value,
                "LineAA<T,F> requires integral T.");
  static_assert(std::is_floating_point<Float>::value,
                "LineAA<T,F> requires floating-point F.");

  // floor and round are each overloaded on float and double.
  auto ipart = [](Float x) -> Int { return Int(std::floor(x)); };
  auto round = [](Float x) -> Float { return std::round(x); };
  auto fpart = [](Float x) -> Float { return x - std::floor(x); };
  auto rfpart = [fpart](Float x) -> Float { return Float(1.0f) - fpart(x); };

  const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
  if (steep) {
    std::swap(x0,y0);
    std::swap(x1,y1);
  }
  if (x0 > x1) {
    std::swap(x0,x1);
    std::swap(y0,y1);
  }

  const Float dx = x1 - x0;
  const Float dy = y1 - y0;
  const Float gradient = (dx == 0) ? 1 : dy / dx;

  Int xpx11;
  Float intery;
  {
    const Float xend = round(x0);
    const Float yend = y0 + gradient * (xend - x0);
    const Float xgap = rfpart(x0 + 0.5);
    xpx11 = Int(xend);
    const Int ypx11 = ipart(yend);
    if (steep) {
      drawpixel(ypx11, xpx11, rfpart(yend) * xgap);
      drawpixel(ypx11 + 1, xpx11, fpart(yend) * xgap);
    } else {
      drawpixel(xpx11, ypx11, rfpart(yend) * xgap);
      drawpixel(xpx11, ypx11 + 1, fpart(yend) * xgap);
    }
    intery = yend + gradient;
  }

  Int xpx12;
  {
    const Float xend = round(x1);
    const Float yend = y1 + gradient * (xend - x1);
    const Float xgap = rfpart(x1 + 0.5);
    xpx12 = Int(xend);
    const Int ypx12 = ipart(yend);
    if (steep) {
      drawpixel(ypx12, xpx12, rfpart(yend) * xgap);
      drawpixel(ypx12 + 1, xpx12, fpart(yend) * xgap);
    } else {
      drawpixel(xpx12, ypx12, rfpart(yend) * xgap);
      drawpixel(xpx12, ypx12 + 1, fpart(yend) * xgap);
    }
  }

  if (steep) {
    for (Int x = xpx11 + 1; x < xpx12; x++) {
      drawpixel(ipart(intery), x, rfpart(intery));
      drawpixel(ipart(intery) + 1, x, fpart(intery));
      intery += gradient;
    }
  } else {
    for (Int x = xpx11 + 1; x < xpx12; x++) {
      drawpixel(x, ipart(intery), rfpart(intery));
      drawpixel(x, ipart(intery) + 1, fpart(intery));
      intery += gradient;
    }
  }
}

template<class Num>
std::optional<std::pair<float, float>> LineIntersection(
    // First segment
    Num p0x, Num p0y, Num p1x, Num p1y,
    // Second segment
    Num p2x, Num p2y, Num p3x, Num p3y) {

  const auto s1x = p1x - p0x;
  const auto s1y = p1y - p0y;
  const auto s2x = p3x - p2x;
  const auto s2y = p3y - p2y;

  const auto l1 = p0x - p2x;
  const auto l2 = p0y - p2y;
  const float denom = s1x * s2y - s2x * s1y;

  const float s = (s1x * l2 - s1y * l1) / denom;

  if (s >= 0.0f && s <= 1.0f) {
    const float t = (s2x * l2 - s2y * l1) / denom;

    if (t >= 0.0f && t <= 1.0f) {
      return {{(float)p0x + (t * s1x),
               (float)p0y + (t * s1y)}};
    }
  }
  return std::nullopt;
}


inline std::pair<float, float>
ClosestPointOnSegment(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y) {
  auto SqDist = [](float x0, float y0,
                   float x1, float y1) {
      const float dx = x1 - x0;
      const float dy = y1 - y0;
      return dx * dx + dy * dy;
    };

  const float sqlen = SqDist(x0, y0, x1, y1);
  if (sqlen == 0.0) {
    // Degenerate case where line segment is just a point,
    // so there is only one choice.
    return {x0, y0};
  }

  const float tf = ((x - x0) * (x1 - x0) + (y - y0) * (y1 - y0)) / sqlen;
  // Make sure it is on the segment.
  const float t = std::max(0.0f, std::min(1.0f, tf));
  // Closest point, which is on the segment.

  const float xx = x0 + t * (x1 - x0);
  const float yy = y0 + t * (y1 - y0);
  return {xx, yy};
}

// Return the minimum distance between the point and the line segment.
inline float PointLineDistance(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y) {

  const auto [xx, yy] = ClosestPointOnSegment(x0, y0, x1, y1, x, y);
  const float dx = x - xx;
  const float dy = y - yy;
  return sqrtf(dx * dx + dy * dy);
}

// Same, but for a line that's known to be horizontal.
inline float PointHorizLineDistance(
    // Line segment
    float x0, float y0, float x1, /* y1 = y0 */
    // Point to test
    float x, float y) {
  // Put in order so that x0 < x1.
  if (x0 > x1) std::swap(x0, x1);
  const float dy = y0 - y;
  if (x <= x0) {
    // Distance is to left vertex.
    const float dx = x0 - x;
    return sqrtf(dx * dx + dy * dy);
  } else if (x >= x1) {
    // To right vertex.
    const float dx = x1 - x;
    return sqrtf(dx * dx + dy * dy);
  } else {
    // Perpendicular to segment itself.
    return fabsf(dy);
  }
}

// ... and vertical.
inline float PointVertLineDistance(
    // Line segment
    float x0, float y0, /* x1 = x0 */ float y1,
    // Point to test
    float x, float y) {
  // Put in order so that y0 < y1.
  if (y0 > y1) std::swap(y0, y1);
  const float dx = x0 - x;
  if (y <= y0) {
    // Distance is to top vertex.
    const float dy = y0 - y;
    return sqrtf(dx * dx + dy * dy);
  } else if (y >= y1) {
    // To bottom corner.
    const float dy = y1 - y;
    return sqrtf(dx * dx + dy * dy);
  } else {
    // Perpendicular to segment itself.
    return fabsf(dx);
  }
}


template<class Num>
std::pair<Num, Num> ReflectPointAboutLine(
    // Line segment
    Num x0, Num y0,
    Num x1, Num y1,
    // Point to reflect
    Num x, Num y) {

  Num dx = x1 - x0;
  Num dy = y1 - y0;
  Num dxs = dx * dx;
  Num dys = dy * dy;
  Num denom = dxs + dys;
  Num a = (dxs - dys) / denom;
  Num b = Num(2) * dx * dy / denom;

  Num x2  = a * (x - x0) + b * (y - y0) + x0;
  Num y2  = b * (x - x0) - a * (y - y0) + y0;

  return std::make_pair(x2, y2);
}


template<class Num>
inline std::vector<std::pair<Num, Num>> TesselateQuadraticBezier(
    // starting vertex
    Num x0, Num y0,
    // control point
    Num x1, Num y1,
    // end point
    Num x2, Num y2,
    Num max_error_squared,
    int max_depth) {

  static_assert(std::is_arithmetic<Num>::value,
                "TesselateQuadraticBezier needs an integral or floating-point "
                "template argument.");

  std::vector<std::pair<Num, Num>> out;
  std::function<void(Num, Num, Num, Num, Num, Num, int)> Rec =
    [&out, max_error_squared, &Rec](Num x0, Num y0,
                                    Num x1, Num y1,
                                    Num x2, Num y2,
                                    int max_depth) {
      // This is based on public-domain code from stb_truetype; thanks!

      // Midpoint of the curve.
      // ("Midpoint" here likely means t/2, not the geometric midpoint?
      // So this might be overly conservative, in that we might have
      // a good approximation to a line but not pass near the line's
      // midpoint at the curve's midpoint. (Consider the case where the
      // control point is on the line, near one of the endpoints.))
      const Num mx = (x0 + (x1 * 2) + x2) / 4;
      const Num my = (y0 + (y1 * 2) + y2) / 4;

      // Midpoint of a straight line.
      const Num lx = (x0 + x2) / 2;
      const Num ly = (y0 + y2) / 2;

      // Error.
      const Num dx = lx - mx;
      const Num dy = ly - my;
      const Num error = (dx * dx) + (dy * dy);

      if (error > max_error_squared && max_depth > 0) {
        Rec(x0, y0, (x0 + x1) / 2, (y0 + y1) / 2, mx, my, max_depth - 1);
        Rec(mx, my, (x1 + x2) / 2, (y1 + y2) / 2, x2, y2, max_depth - 1);
      } else {
        // Otherwise, emit a straight line.
        out.emplace_back(x2, y2);
      }
    };

  Rec(x0, y0, x1, y1, x2, y2, max_depth);
  return out;
}


#endif
