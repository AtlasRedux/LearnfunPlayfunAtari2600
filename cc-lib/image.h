// Images residing in RAM as vectors of pixels.
// Goal is to be simple and portable.

#ifndef _CC_LIB_IMAGE_H
#define _CC_LIB_IMAGE_H

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <span>
#include <string_view>

#include <version>

// ImageRGBA is recommended for most uses, but other formats are
// supported below.
struct ImageRGB;
struct ImageA;
struct ImageF;
struct Image1;

// 4-channel image in R-G-B-A order.
// uint32s are represented like 0xRRGGBBAA irrespective of native
// byte order.
struct ImageRGBA {
  using uint8 = uint8_t;
  using uint32 = uint32_t;
  ImageRGBA(const std::vector<uint8> &rgba8, int width, int height);
  ImageRGBA(const std::vector<uint32> &rgba32, int width, int height);
  ImageRGBA(std::vector<uint32> &&rgba, int width, int height);
  ImageRGBA(int width, int height);
  ImageRGBA() : width(0), height(0) {}

  // TODO: copy/assignment

  // Requires equality of RGBA values (even if alpha is zero).
  bool operator ==(const ImageRGBA &other) const;
  // Deterministic hash, but not intended to be stable across
  // invocations.
  std::size_t Hash() const;

  int Width() const { return width; }
  int Height() const { return height; }

  // Supports PNG, JPEG, GIF, others (see stb_image.h).
  static ImageRGBA *Load(std::string_view filename);
  static ImageRGBA *LoadFromMemory(const std::vector<uint8> &bytes);
  static ImageRGBA *LoadFromMemory(const char *data, size_t size);

  // Saves in RGBA PNG format. Returns true if successful.
  bool Save(std::string_view filename) const;
  std::vector<uint8> SaveToVec() const;
  std::string SaveToString() const;

  // Quality in [1, 100]. Returns true if successful.
  bool SaveJPG(std::string_view filename, int quality = 90) const;
  // TODO: jpg to vec, to string

  // TODO: Replace with copy constructor
  ImageRGBA *Copy() const;
  // Crop (or pad), returning a new image of the given width and height.
  // If this includes any area outside the input image, fill with
  // fill_color.
  ImageRGBA Crop32(int x, int y, int w, int h,
                   uint32 fill_color = 0x00000000) const;

  // Scale by a positive integer factor, crisp pixels.
  ImageRGBA ScaleBy(int scale) const;
  ImageRGBA ScaleBy(int xscale, int yscale) const;
  // Scale down by averaging boxes of size scale x scale to produce
  // a pixel value. If the width and height are not divisible by
  // the scale, pixels are dropped.
  ImageRGBA ScaleDownBy(int scale) const;

  // In RGBA order, where R value is MSB. Pixels outside the image
  // read as 0,0,0,0.
  inline uint32 GetPixel32(int x, int y) const;
  inline std::tuple<uint8, uint8, uint8, uint8> GetPixel(int x, int y) const;

  // Clear the image to a single value.
  void Clear(uint8 r, uint8 g, uint8 b, uint8 a);
  void Clear32(uint32 rgba);

  inline void SetPixel(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a);
  inline void SetPixel32(int x, int y, uint32 rgba);

  // Blend pixel with existing data.
  // Note: Currently assumes existing alpha is 0xFF.
  void BlendPixel(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendPixel32(int x, int y, uint32 color);

  // TODO:
  // Blend a filled rectangle with sub-pixel precision. Clips.
  // void BlendRectSub32(float x, float y, float w, float h, uint32 color);

  // Blend a filled rectangle. Clips.
  void BlendRect(int x, int y, int w, int h,
                 uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendRect32(int x, int y, int w, int h, uint32 color);
  // Set to the pixel value.
  void FillRect(int x, int y, int w, int h,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  void FillRect32(int x, int y, int w, int h, uint32_t color);

  // Hollow box, one pixel width.
  // nullopt corner_color = color for a crisp box, but setting
  // the corners to 50% alpha makes a subtle roundrect effect.
  void BlendBox32(int x, int y, int w, int h,
                  uint32 color, std::optional<uint32> corner_color);

  // Embedded 9x9 pixel font.
  static constexpr int TEXT_WIDTH  = 9;
  static constexpr int TEXT_HEIGHT = 9;
  // Supports ANSI color codes (only basic ones; experimental).
  // XXX: Only these two routines work with ANSI today.
  void BlendText(int x, int y,
                 uint8 r, uint8 g, uint8 b, uint8 a,
                 std::string_view s);
  void BlendText32(int x, int y, uint32 color, std::string_view s);

  // Draws an outline by first drawing on the 9-connected neighbors
  // in the background color. The foreground color is composited on
  // top of the background, so opaque foreground will look best.
  void BlendTextOutline32(int x, int y,
                          uint32_t outline_color,
                          uint32_t fg_color,
                          std::string_view s);

  // x specifies the left edge, whether up or down. y specifies the
  // bottom if up, or top if down.
  void BlendTextVert32(int x, int y, bool up,
                       uint32 color, std::string_view s);

  // Same font, but scaled to (crisp) 2x2 pixels.
  static constexpr int TEXT2X_WIDTH  = TEXT_WIDTH * 2;
  static constexpr int TEXT2X_HEIGHT = TEXT_HEIGHT * 2;
  void BlendText2x(int x, int y,
                   uint8 r, uint8 g, uint8 b, uint8 a,
                   std::string_view s);
  void BlendText2x32(int x, int y, uint32 color,
                     std::string_view s);

  void BlendTextVert2x32(int x, int y, bool up,
                         uint32 color, std::string_view s);

  // Clipped. Alpha blending.
  // This draws a crisp pixel line using Bresenham's algorithm.
  // Includes start and end point. (TODO: Version that does not include
  // the endpoint, for polylines with alpha.)
  // Note: Clipping is linear time (PERF: Fix!).
  void BlendLine(int x1, int y1, int x2, int y2,
                 uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendLine32(int x1, int y1, int x2, int y2, uint32 color);

  // PERF: This is slow (rasterizes the entire bounding box).
  void BlendThickLine32(float x1, float y1, float x2, float y2, float radius,
                        uint32 color);

  // Clipped. Alpha blending.
  // Blends an anti-aliased line using Wu's algorithm; slower.
  // Endpoints are pixel coordinates, but can be sub-pixel.
  void BlendLineAA(float x1, float y1, float x2, float y2,
                   uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendLineAA32(float x1, float y1, float x2, float y2, uint32 color);

  void BlendFilledCircle32(int x, int y, int r, uint32 color);
  void BlendFilledCircleAA32(float x, float y, float r, uint32 color);

  void BlendCircle32(int x, int y, int r, uint32 color);
  // PERF: Slow (rasterizes the entire bounding box).
  void BlendThickCircle32(float x, float y, float circle_radius,
                          float line_radius, uint32 color);
  // Even slower.
  void BlendThickCircleAA32(float x, float y, float circle_radius,
                            float line_radius, uint32 color);

  // Blend a filled triangle. Either winding order.
  // PERF: Slow (rasterizes the entire bounding box).
  void BlendTriangle32(int x0, int y0, int x1, int y1, int x2, int y2,
                       uint32_t color);

  // Clipped, alpha blending.
  void BlendImage(int x, int y, const ImageRGBA &other);
  void BlendImageRect(int destx, int desty, const ImageRGBA &other,
                      int srcx, int srcy, int srcw, int srch);
  // Clipped, but copy source alpha and ignore current image contents.
  void CopyImage(int x, int y, const ImageRGBA &other);
  void CopyImageRect(int destx, int desty, const ImageRGBA &other,
                     int srcx, int srcy, int srcw, int srch);

  // Output value is RGBA floats in [0, 255].
  // Alpha values are just treated as a fourth channel and averaged,
  // which is not really correct.
  // Treats the input pixels as being "located" at their top-left
  // corners (not their centers).
  // x/y out of bounds will repeat edge pixels.
  std::tuple<float, float, float, float>
  SampleBilinear(float x, float y) const;

  // Extract single channel.
  ImageA Red() const;
  ImageA Green() const;
  ImageA Blue() const;
  ImageA Alpha() const;

  // Compute the perceptual lightness. Assumes sRGB width D65
  // reference white. Ignores alpha channel.
  ImageA Lightness() const;

  // Ignore the alpha channel.
  ImageRGB IgnoreAlpha() const;
  // TODO: Remove alpha with matte. Ignores alpha component
  // of matte_rgba.
  ImageRGB CompositeOntoMatte(uint32_t matte_rgba) const;

  // Images must all be the same dimensions.
  static ImageRGBA FromChannels(const ImageA &red,
                                const ImageA &green,
                                const ImageA &blue,
                                const ImageA &alpha);

  // Convert to R-G-B-A byte order.
  std::vector<uint8_t> ToBuffer8() const;

  // View of the underlying words. There should be Width() * Height()
  // pixels, each packed R-G-B-A regardless of host byte order.
#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
  std::span<const uint32_t> data() const;
  std::span<uint32_t> data();
#endif

 private:
  friend struct ImageResize;

  int width, height;
  // Size width * height.
  // Bytes are packed 0xRRGGBBAA, regardless of host endianness.
  std::vector<uint32_t> rgba;
};

// 24-bit image with alpha=0xFF. This is primarily used as a
// conversion format; if you want to draw on it, use ImageRGBA.
struct ImageRGB {
  using uint8 = uint8_t;
  ImageRGB(std::vector<uint8> rgb, int width, int height);
  ImageRGB(int width, int height);
  // Empty image sometimes useful for filling vectors, etc.
  ImageRGB() : ImageRGB(0, 0) {}
  // Value semantics.
  ImageRGB(const ImageRGB &other) = default;
  ImageRGB(ImageRGB &&other) = default;
  ImageRGB &operator =(const ImageRGB &other) = default;
  ImageRGB &operator =(ImageRGB &&other) = default;

  bool operator ==(const ImageRGB &other) const;
  // Deterministic hash, but not intended to be stable across
  // invocations.
  std::size_t Hash() const;

  // TODO: maybe loading?
  bool SavePNG(const std::string &filename) const;
  std::vector<uint8> SavePNGToVec() const;
  std::string SavePNGToString() const;

  bool SaveJPG(const std::string &filename, int quality = 90) const;
  std::vector<uint8> SaveJPGToVec(int quality = 90) const;
  std::string SaveJPGToString(int quality = 90) const;

  void Clear(uint8 r, uint8 g, uint8 b);
  // Ignores alpha component.
  void Clear32(uint32_t rgba);

  // Clipped.
  inline void SetPixel(int x, int y, uint8 r, uint8 g, uint8 b);
  // x/y must be in bounds.
  inline std::tuple<uint8, uint8, uint8> GetPixel(int x, int y) const;

  int Width() const { return width; }
  int Height() const { return height; }

  // Alpha blending onto the RGB surface.
  void BlendImage(int x, int y, const ImageRGBA &other);
  void BlendImageRect(int destx, int desty, const ImageRGBA &other,
                      int srcx, int srcy, int srcw, int srch);

  void BlendPixel(int x, int y, uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendPixel32(int x, int y, uint32_t color);

  // Convert to RGBA format with a constant alpha value; typically 0xFF.
  ImageRGBA AddAlpha(uint8_t a = 0xFF) const;

  // R-G-B byte order.
  std::vector<uint8> ToBuffer8() const;

  // TODO: Extract individual channels, or build from channels.

private:
  // Bytes are packed RR,GG,BB, RR,GG,BB, ... regardless of host
  // endianness.
  int width = 0, height = 0;
  std::vector<uint8_t> rgb;
};

// Single-channel 8-bit bitmap.
struct ImageA {
  using uint8 = uint8_t;
  ImageA(std::vector<uint8> alpha, int width, int height);
  ImageA(int width, int height);
  // Empty image sometimes useful for filling vectors, etc.
  ImageA() : ImageA(0, 0) {}
  // Value semantics.
  ImageA(const ImageA &other) = default;
  ImageA(ImageA &&other) = default;
  ImageA &operator =(const ImageA &other) = default;
  ImageA &operator =(ImageA &&other) = default;

  bool operator ==(const ImageA &other) const;
  // Deterministic hash, but not intended to be stable across
  // invocations.
  std::size_t Hash() const;

  int Width() const { return width; }
  int Height() const { return height; }

  ImageA *Copy() const;
  // Scale by a positive integer factor, crisp pixels.
  ImageA ScaleBy(int scale) const;
  ImageA ScaleBy(int xscale, int yscale) const;
  // Generally appropriate for enlarging, not shrinking.
  ImageA ResizeBilinear(int new_width, int new_height) const;
  // Nearest-neighbor; works "fine" for enlarging and shrinking.
  ImageA ResizeNearest(int new_width, int new_height) const;
  // Make a four-channel image of the same size, R=v, G=v, B=v, A=0xFF.
  ImageRGBA GreyscaleRGBA() const;
  // Make a four-channel alpha mask of the same size, RGB=rgb, A=v
  ImageRGBA AlphaMaskRGBA(uint8 r, uint8 g, uint8 b) const;

  // Generate a bitmap image where there's a 1 pixel whenever the
  // value is >= min_one.
  Image1 Threshold(uint8_t min_one = 1) const;

  // Only increases values.
  void BlendText(int x, int y, uint8 v, std::string_view s);

  void Clear(uint8 value);

  void BlendImage(int x, int y, const ImageA &other);
  // Clipped, but copy source alpha and ignore current image contents.
  void CopyImage(int x, int y, const ImageA &other);

  // Clipped.
  inline void SetPixel(int x, int y, uint8 v);
  inline uint8 GetPixel(int x, int y) const;
  inline void BlendPixel(int x, int y, uint8 v);

  // Output value is a float in [0, 255].
  // Treats the input pixels as being "located" at their top-left
  // corners (not their centers).
  // x/y out of bounds will repeat edge pixels.
  float SampleBilinear(float x, float y) const;

private:
  int width, height;
  // Size width * height.
  std::vector<uint8> alpha;
};

// Like ImageA, but float pixel values. The nominal range is [0, 1],
// but this supports "HDR" pixels, including even negative values.
struct ImageF {
  ImageF(const std::vector<float> &alpha, int width, int height);
  ImageF(int width, int height);
  ImageF() : ImageF(0, 0) {}
  explicit ImageF(const ImageA &other);
  // Value semantics.
  ImageF(const ImageF &other) = default;
  ImageF(ImageF &&other) = default;
  ImageF &operator =(const ImageF &other) = default;
  ImageF &operator =(ImageF &&other) = default;

  int Width() const { return width; }
  int Height() const { return height; }

  // Generally appropriate for enlarging, not shrinking.
  ImageF ResizeBilinear(int new_width, int new_height) const;

  // Convert to 8-bit ImageA, rounding. This clamps pixel
  // values to [0, 1].
  ImageA Make8Bit() const;

  // Only increases values.
  void BlendText(int x, int y, float v, std::string_view s);

  void Clear(float value);

  // Clamps to [0, 1] in place.
  void Clamp();
  // Normalizes (linearly) such that the darkest pixel has value 0.0f
  // and the lightest has 1.0f. If all pixels are the same, the result
  // will be all 0.0f.
  void Normalize();

  // Clipped.
  inline void SetPixel(int x, int y, float v);
  inline float GetPixel(int x, int y) const;
  inline void BlendPixel(int x, int y, float v);

  // Treats the input pixels as being "located" at their top-left
  // corners (not their centers).
  // x/y out of bounds will repeat edge pixels.
  float SampleBilinear(float x, float y) const;
  // Same but with an implied value (usually 0 or 1) outside the
  // image.
  float SampleBilinear(float x, float y, float outside_value) const;

private:
  int width, height;
  // Size width * height.
  std::vector<float> alpha;
};


// Single-channel 1-bit bitmap.
struct Image1 {
  using uint8 = uint8_t;

  // Consider creating these with ImageA::Threshold.

  Image1(const std::vector<bool> &alpha, int width, int height);
  Image1(int width, int height);
  // Empty image sometimes useful for filling vectors, etc.
  Image1() : Image1(0, 0) {}
  // Value semantics.
  Image1(const Image1 &other) = default;
  Image1(Image1 &&other) = default;

  Image1 &operator =(const Image1 &other) = default;
  Image1 &operator =(Image1 &&other) = default;

  bool operator ==(const Image1 &other) const;
  // Deterministic hash, but not intended to be stable across
  // invocations.
  std::size_t Hash() const;

  int Width() const { return width; }
  int Height() const { return height; }

  // Make a four-channel image of the same size, using the given
  // colors for 1 and 0.
  ImageRGBA MonoRGBA(uint32_t one = 0xFFFFFFFF,
                     uint32_t zero = 0x00000000) const;
  ImageA MonoA(uint8_t one = 0xFF, uint8_t zero = 0x00) const;

  void Clear(bool value = false);

  // TODO: Can easily support logical operations on images that
  // are the same size. But maybe I want that to be generalized
  // to regions.
  Image1 Inverse() const;

  // Clipped.
  inline void SetPixel(int x, int y, bool v);
  inline bool GetPixel(int x, int y) const;
  void CopyImage(int x, int y, const Image1 &other);
  void CopyImageRect(int destx, int desty, const Image1 &other,
                     int srcx, int srcy, int srcw, int srch);

  void SetRect(int x, int y, int w, int h, bool value = true);

private:
  int width, height;
  // Size width * height / 64.
  // Only padded at the end (with zeroes); a word can span more than
  // one pixel row.
  // PERF: Consider different representations here. It could be that
  // a vector of bytes is just faster, for example.
  std::vector<uint64_t> bits;

  // No bounds checking.
  inline bool Sub(int px) const;
  inline void Set(int px, bool b);

  // The number of 64-bit words we need to represent the given
  // number of pixels.
  static int NumWords(int pixels);
  // If any bits outside of the image region were modified, call this
  // to restore the representation invariant by zeroing them.
  void CanonicalMask();
};


// Implementations follow.


uint32_t ImageRGBA::GetPixel32(int x, int y) const {
  // Treat out-of-bounds reads as containing 00,00,00,00.
  if ((unsigned)x >= (unsigned)width) return 0;
  if ((unsigned)y >= (unsigned)height) return 0;
  const int base = y * width + x;
  return rgba[base];
}

std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
ImageRGBA::GetPixel(int x, int y) const {
  // Treat out-of-bounds reads as containing 00,00,00,00.
  if ((unsigned)x >= (unsigned)width ||
      (unsigned)y >= (unsigned)height)
    return std::make_tuple(0, 0, 0, 0);

  uint32 color = rgba[y * width + x];
  return std::make_tuple(
      (uint8_t)((color >> 24) & 255),
      (uint8_t)((color >> 16) & 255),
      (uint8_t)((color >> 8) & 255),
      (uint8_t)(color & 255));
}

void ImageRGBA::SetPixel(int x, int y,
                         uint8 r, uint8 g, uint8 b, uint8 a) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;

  const uint32 color =
    ((uint32)r << 24) | ((uint32)g << 16) | ((uint32)b << 8) | (uint32)a;

  rgba[y * width + x] = color;
}

void ImageRGBA::SetPixel32(int x, int y, uint32 color) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  rgba[y * width + x] = color;
}


void ImageRGB::SetPixel(int x, int y, uint8 r, uint8 g, uint8 b) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  const int idx = ((y * width) + x) * 3;
  rgb[idx + 0] = r;
  rgb[idx + 1] = g;
  rgb[idx + 2] = b;
}

std::tuple<uint8_t, uint8_t, uint8_t> ImageRGB::GetPixel(int x, int y) const {
  // Treat out-of-bounds reads as containing 00,00,00.
  if ((unsigned)x >= (unsigned)width ||
      (unsigned)y >= (unsigned)height) return std::make_tuple(0, 0, 0);
  const int idx = ((y * width) + x) * 3;
  return std::make_tuple(rgb[idx + 0],
                         rgb[idx + 1],
                         rgb[idx + 2]);
}


uint8_t ImageA::GetPixel(int x, int y) const {
  if ((unsigned)x >= (unsigned)width) return 0;
  if ((unsigned)y >= (unsigned)height) return 0;
  return alpha[y * width + x];
}

void ImageA::SetPixel(int x, int y, uint8_t value) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  alpha[y * width + x] = value;
}

void ImageA::BlendPixel(int x, int y, uint8_t v) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  // XXX test this blending math
  uint8_t old = GetPixel(x, y);
  uint16_t opaque_part = 255 * v;
  uint16_t transparent_part = (255 - v) * old;
  uint8_t new_value =
    0xFF & ((opaque_part + transparent_part) / (uint16_t)255);
  SetPixel(x, y, new_value);
}


float ImageF::GetPixel(int x, int y) const {
  if ((unsigned)x >= (unsigned)width) return 0.0f;
  if ((unsigned)y >= (unsigned)height) return 0.0f;
  return alpha[y * width + x];
}

void ImageF::SetPixel(int x, int y, float value) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  alpha[y * width + x] = value;
}

void ImageF::BlendPixel(int x, int y, float value) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  const float old = GetPixel(x, y);
  const float opaque_part = value;
  const float transparent_part = (1.0f - value) * old;
  SetPixel(x, y, opaque_part + transparent_part);
}


bool Image1::Sub(int px) const {
  const uint64_t w = bits[px >> 6];
  const uint64_t sel = uint64_t{1} << (63 - (px & 63));
  return !!(w & sel);
}

void Image1::Set(int px, bool b) {
  // PERF: Make sure the compiler does this without branches.
  const uint64_t sel = uint64_t{1} << (63 - (px & 63));
  if (b) {
    bits[px >> 6] |= sel;
  } else {
    bits[px >> 6] &= ~sel;
  }
}


bool Image1::GetPixel(int x, int y) const {
  if ((unsigned)x >= (unsigned)width) return 0;
  if ((unsigned)y >= (unsigned)height) return 0;
  return Sub(y * width + x);
}

void Image1::SetPixel(int x, int y, bool value) {
  if ((unsigned)x >= (unsigned)width) return;
  if ((unsigned)y >= (unsigned)height) return;
  Set(y * width + x, value);
}

#endif
