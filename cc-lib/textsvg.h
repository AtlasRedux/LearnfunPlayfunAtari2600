
#ifndef _CC_LIB_TEXTSVG_H
#define _CC_LIB_TEXTSVG_H

#include <string>
#include <utility>
#include <vector>

struct TextSVG {
  // Uses px and sets origin at 0,0.
  static std::string Header(double width, double height);
  static std::string HeaderEx(double x, double y,
                              double width, double height,
                              const std::string &units,
                              const std::string &generator);
  static std::string Footer();

  // Returns a string containing a number suitable for SVG (with
  // up to 5 digits of precision); strips unnecessary leading and
  // trailing zeroes.
  static std::string Rtos(double d);
  
  // text vector is like
  // {{"#FFFFFF", "this text is "}, {"#0000FF", "blue"}}
  static std::string Text(
      double x, double y,
      const std::string &face,
      double size,
      const std::vector<std::pair<std::string, std::string>> &text);

  // When outputting a series of points from some data source, it is
  // common to have many colinear subsequences. This eliminates such
  // points so that the SVG can be more efficient. max_error is the
  // distance (from the line) threshold to apply.
  static std::vector<std::pair<double, double>> RemoveColinear(
      const std::vector<std::pair<double, double>> &points,
      double max_error);
};

#endif
