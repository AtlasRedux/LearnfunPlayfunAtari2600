#ifndef _CC_LIB_COLOR_UTIL_H
#define _CC_LIB_COLOR_UTIL_H

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <tuple>

// Convenience for specifying rows in gradients as 0xRRGGBB.
// This should be a member of ColorUtil but is not allowed, perhaps
// because of a compiler bug?
static inline constexpr
std::tuple<float, float, float, float> GradRGB(float f, uint32_t rgb) {
  return std::tuple<float, float, float, float>(
      f,
      ((rgb >> 16) & 255) * (1.0f / 255.0f),
      ((rgb >>  8) & 255) * (1.0f / 255.0f),
      ( rgb        & 255) * (1.0f / 255.0f));
}

struct ColorUtil {
  // hue, saturation, value nominally in [0, 1].
  // hue of 0.0 is red.
  // RGB output also in [0, 1].
  static void HSVToRGB(float hue, float saturation, float value,
                       float *r, float *g, float *b);
  static std::tuple<float, float, float>
  HSVToRGB(float hue, float saturation, float value);
  static uint32_t HSVAToRGBA32(
      float hue, float saturation, float value, float alpha);


  // For RGB values in [0, 1].
  static std::tuple<float, float, float>
  RGBToHSV(float r, float g, float b);

  // Convert to CIE L*A*B*.
  // RGB channels are nominally in [0, 1].
  // Here RGB is interpreted as sRGB with a D65 reference white.
  // L* is nominally [0,100]. A* and B* are unbounded but
  // are "commonly clamped to -128 to 127".
  static void RGBToLAB(float r, float g, float b,
                       float *ll, float *aa, float *bb);
  static std::tuple<float, float, float>
  RGBToLAB(float r, float g, float b);

  // Nominal inverse of the above.
  static std::tuple<float, float, float>
  LABToRGB(float lab_l, float lab_a, float lab_b);

  // CIE1994 distance between sample color Lab2 and reference Lab1.
  // ** Careful: This may not even be symmetric! **
  // This is designd such that a Delta E of 1.0 is a "just noticeable"
  // difference, and 100.0 is the nominal maximum. Your mileage may vary.
  // Note: This has been superseded by an even more complicated function
  // (CIEDE2000) if you are doing something very sensitive.
  static float DeltaE(float l1, float a1, float b1,
                      float l2, float a2, float b2);

  static constexpr std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
  Unpack32(uint32_t rgba);

  static constexpr uint32_t
  Pack32(uint8_t r, uint8_t g, uint8_t b, uint8_t a);


  // Convert float RGBA components (nominally in [0, 1]; clamped) to
  // 8-bit channels packed as RRGGBBAA.
  static uint32_t FloatsTo32(float r, float g, float b, float a);
  // Inverse of above.
  static std::tuple<float, float, float, float>
  U32ToFloats(uint32_t rgba);

  // Mix 3 color channels (linear interpolation). Note that it is
  // better to mix in a perceptual color space like LAB than in RGB.
  static std::tuple<float, float, float>
  Mix3Channels(float ra, float ga, float ba,
               float rb, float gb, float bb,
               float t);

  // initializer_list so that these can be constexpr.
  using Gradient = std::initializer_list<
    std::tuple<float, float, float, float>>;

  // Three-channel linear gradient with the sample point t and the
  // gradient ramp specified. Typical usage would be for t in [0,1]
  // and RGB channels each in [0,1], but other uses are acceptable
  // (everything is just Euclidean). For t outside the range, the
  // endpoints are returned.
  static std::tuple<float, float, float>
  LinearGradient(const Gradient &ramp, float t);

  // As RGBA, with alpha=0xFF.
  static uint32_t LinearGradient32(const Gradient &ramp, float t);

  static constexpr Gradient HEATED_METAL{
    GradRGB(0.0f, 0x000000),
    GradRGB(0.2f, 0x7700BB),
    GradRGB(0.5f, 0xFF0000),
    GradRGB(0.8f, 0xFFFF00),
    GradRGB(1.0f, 0xFFFFFF)
  };

  // Red for negative, black for 0, green for positive.
  // nominal range [-1, 1].
  static constexpr Gradient NEG_POS{
    GradRGB(-1.0f, 0xFF0000),
    GradRGB( 0.0f, 0x000000),
    GradRGB( 1.0f, 0x00FF00),
  };

  // Like heated metal, but for text on a black background,
  // where even the darkest text is still readable.
  static constexpr Gradient HEATED_TEXT{
    GradRGB(0.0f, 0x222222),
    GradRGB(0.2f, 0x7722BB),
    GradRGB(0.5f, 0xFF2222),
    GradRGB(0.8f, 0xFFFF22),
    GradRGB(1.0f, 0xFFFFFF)
  };

  // ROYGBIV; always visible on black.
  static constexpr Gradient VISIBLE_SPECTRUM{
    GradRGB(0.00f, 0xFF0000),
    GradRGB(0.25f, 0xFFFF00),
    GradRGB(0.50f, 0x00FF00),
    GradRGB(0.75f, 0x0000FF),
    GradRGB(1.00f, 0xFF00FF),
  };

  // TODO: Arrays of color values. A frequent thing I need
  // is "a bunch of distinct colors that look good on a
  // dark background."
};


// inline functions follow

// We divide by 255 to convert to [0.0, 1.0] but multiply by 256 (and
// truncate) to convert back to [0, 255]. This works round-trip for
// all ints, but is there a more principled way that minimizes error?

inline uint32_t ColorUtil::FloatsTo32(float r, float g, float b, float a) {
  // Casting to int truncates towards zero. An exact 1.0 (for example) gets
  // clamped.
  uint32_t rr = std::clamp((int)(r * 256.0f), 0, 255);
  uint32_t gg = std::clamp((int)(g * 256.0f), 0, 255);
  uint32_t bb = std::clamp((int)(b * 256.0f), 0, 255);
  uint32_t aa = std::clamp((int)(a * 256.0f), 0, 255);
  return (rr << 24) | (gg << 16) | (bb << 8) | aa;
}

inline std::tuple<float, float, float, float>
ColorUtil::U32ToFloats(uint32_t rgba) {
  uint8_t r = (rgba >> 24) & 255;
  uint8_t g = (rgba >> 16) & 255;
  uint8_t b = (rgba >> 8) & 255;
  uint8_t a = rgba & 255;

  return std::make_tuple(
      std::clamp(r * (1.0f / 255.0f), 0.0f, 1.0f),
      std::clamp(g * (1.0f / 255.0f), 0.0f, 1.0f),
      std::clamp(b * (1.0f / 255.0f), 0.0f, 1.0f),
      std::clamp(a * (1.0f / 255.0f), 0.0f, 1.0f));
}

inline constexpr std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
ColorUtil::Unpack32(uint32_t color) {
  return {(uint8_t)((color >> 24) & 255),
          (uint8_t)((color >> 16) & 255),
          (uint8_t)((color >> 8) & 255),
          (uint8_t)(color & 255)};
}

inline constexpr uint32_t
ColorUtil::Pack32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return
    ((uint32_t)r << 24) |
    ((uint32_t)g << 16) |
    ((uint32_t)b << 8) |
    (uint32_t)a;
}

#endif

