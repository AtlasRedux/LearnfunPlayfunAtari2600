
#include "textsvg.h"

#include <string>
#include <stdio.h>
#include <utility>
#include <cmath>
#include <vector>

using namespace std;

string TextSVG::HeaderEx(double x, double y,
                         double width, double height,
                         const string &units,
                         const string &generator) {
  string out;
  out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  if (!generator.empty()) out += (string)"<!-- Generator: " +
                            generator + (string)" -->\n";

  string xu = Rtos(x) + units;
  string yu = Rtos(y) + units;
  string wu = Rtos(width) + units;
  string hu = Rtos(height) + units;

  out += "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
         "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" [\n"
         "<!ENTITY ns_flows \"http://ns.adobe.com/Flows/1.0/\">\n"
         "]>\n"
         "<svg version=\"1.1\"\n"
         " xmlns=\"http://www.w3.org/2000/svg\""
         " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
         " xmlns:a=\"http://ns.adobe.com/AdobeSVGViewerExtensions/3.0/\"";
  out += " x=\"" + xu + "\" y=\"" + yu + "\"";
  out += " width=\"" + wu + "\" height=\"" + hu + "\"";
  out += " viewBox=\"" + Rtos(x) + " " + Rtos(y) + " " +
    Rtos(width) + " " + Rtos(height) + "\"";

  out += " xml:space=\"preserve\">\n";
  return out;
}

string TextSVG::Header(double width, double height) {
  return HeaderEx(0.0, 0.0, width, height, "px", "cc-lib/textsvg.cc");
}

string TextSVG::Footer() {
  return "</svg>\n";
}

string TextSVG::Rtos(double d) {
  // This is probably wrong for svg--but what?
  if (std::isnan(d)) return "NaN";

  // Handle minus sign ourselves. Note subtlety about negative
  // zero; testing e.g. (d < 0) fails because c++ treats
  // -0 as == 0, but printf still prints the minus sign.
  const bool negative = std::signbit(d);
  if (negative) d = abs(d);

  char out[17];
  // Make sure there is always room for a minus sign.
  snprintf(out + 1, 16, "%.5f", d);

  // Strip leading zeroes. We always have a nonzero character
  // (even the terminating \0) so this loop terminates.
  char *o = out + 1;
  while (*o == '0') o++;

  // Now strip trailing zeroes and periods. e is where we will
  // truncate the string (place a \0) if we reach the end, or nullptr
  // if we are not looking at a run of zeroes.
  char *e = nullptr;
  bool truncating = false;
  for (char *f = o; *f != '\0'; f++) {
    // Don't do any truncation until we see the period.
    if (*f == '.') {
      truncating = true;
      // Truncate away the decimal point as well.
      e = f;
      continue;
    }

    if (truncating) {
      if (e != nullptr) {
        // Have a run of zeroes. Stop?
        if (*f != '0')
          e = nullptr;
      } else {
        // No run. Start?
        if (*f == '0')
          e = f;
      }
    }
  }

  if (e != nullptr) *e = '\0';

  // If we stripped all the leading and trailing zeroes and the
  // decimal point, then the input was rounded to zero. We do this
  // rather than testing for zero up front because numbers very close
  // to zero still get rounded.
  if (*o == '\0')
    return "0";

  if (negative) {
    // Okay even if we didn't strip zeroes, since we wrote to
    // out + 1 above.
    o--;
    *o = '-';
  }

  return string{o};
}

static string Replace(string src, const string &findme, const string &rep) {
  auto idx = src.length() - 1;

  if (findme.length() < 1) return src;

  /* idx represents the position in src which, for all chars greater
     than it, there begins no match of findme */
  for (;;) {
    idx = src.rfind(findme, idx);
    if (idx == string::npos)
      break;
    /* do replacement */
    src.replace(idx, findme.length(), rep);
    /* don't allow any matches to extend into the string we just inserted;
       (consider replacing "abc" with "bcd" in the string "aabc") */
    if (findme.length() > idx)
      break;
    idx -= findme.length();
  }
  return src;
}

// XXX Need to escape for SVG. This is insufficient.
static string Escape(string s) {
  s = Replace(s, "<", "&lt;");
  s = Replace(s, ">", "&gt;");
  s = Replace(s, "\"", "&quot;");
  return s;
}

string TextSVG::Text(
    double x, double y,
    const string &face,
    double size,
    const std::vector<std::pair<std::string, std::string>> &text) {
  auto OneText = [](pair<string, string> p) {
    return (string)"<tspan fill=\"" + p.first + "\">" +
      Escape(p.second) + "</tspan>";
  };

  string ret =
    "<text x=\"" + Rtos(x) + "\" y=\"" + Rtos(y) + "\" font-family=\"" +
    face + "\" font-size=\"" + Rtos(size) + "\">";
  for (const auto &p : text)
    ret += OneText(p);
  ret += "</text>";
  return ret;
}

struct ColinearRemover {
  explicit ColinearRemover(double max_error) :
    max_error(max_error),
    sq_max_error(max_error * max_error) {}

  enum State {
    EMPTY,
    ONE_POINT,
    LINE,
  };

  static inline double SqDist(pair<double, double> pt1,
                              pair<double, double> pt2) {
    double dx = pt2.first - pt1.first;
    double dy = pt2.second - pt1.second;
    return dx * dx + dy * dy;
  }

  void Push(pair<double, double> pt,
            vector<pair<double, double>> *out) {
    switch (state) {
    case EMPTY:
      // Always take a starting point.
      a = pt;
      state = ONE_POINT;
      return;
    case ONE_POINT: {
      const double sqdist = SqDist(a, pt);
      if (sqdist > max_error) {
        b = pt;
        state = LINE;
        // PERF precompute some info about line here?
        return;
      }
      return;
    }

    case LINE: {
      const double x0 = pt.first, y0 = pt.second;
      const double x1 = a.first, y1 = a.second;
      const double x2 = b.first, y2 = b.second;
      const double numer =
        abs((y2 - y1) * x0 - (x2 - x1) * y0 + x2 * y1 - y2 * x1);
      const double line_length = sqrt(SqDist(a, b));
      const double dist = numer / line_length;
      if (dist < max_error) {
        // Drop the interior point.
        // XXX: This is actually wrong if the third point is between
        // the first two. Should test for this, and shift in this case.
        b = pt;
        return;
      } else {
        // Shift.
        out->push_back(a);
        a = b;
        b = pt;
        return;
      }
    }
    }
  }

  void Flush(vector<pair<double, double>> *out) {
    switch (state) {
    case EMPTY: return;
    case ONE_POINT:
      out->push_back(a);
      break;
    case LINE:
      out->push_back(a);
      out->push_back(b);
    }
  }

  const double max_error = 0.0, sq_max_error = 0.0;
  State state = EMPTY;
  pair<double, double> a{0.0, 0.0};
  pair<double, double> b{0.0, 0.0};
};


vector<pair<double, double>> TextSVG::RemoveColinear(
    const vector<pair<double, double>> &points,
    double max_error) {
  vector<pair<double, double>> out;
  // Not reserving space, because we may make very dramatic reductions.

  ColinearRemover remover{max_error};

  for (const auto &p : points)
    remover.Push(p, &out);

  remover.Flush(&out);
  return out;
}
