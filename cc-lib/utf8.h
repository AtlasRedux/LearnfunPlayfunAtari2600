
// Basic header-only UTF-8 routines. These are often needed in
// unrelated libraries, so they are here with minimal dependencies.

#ifndef _CC_LIB_UTF8_H
#define _CC_LIB_UTF8_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct UTF8 {
  // Encode the codepoint as a 1-4 byte string. Returns the empty string
  // if the codepoint is out of range.
  static inline constexpr std::string Encode(uint32_t codepoint);

  static constexpr uint32_t REPLACEMENT_CODEPOINT = 0xFFFD;
  // Decode the string as a vector of codepoints, assuming it is
  // encoded correctly. This treats no codepoints specially. Invalid
  // encodings are transformed to U+FFFD, the replacement character,
  // but the algorithm for doing this is unspecified.
  static inline std::vector<uint32_t> Codepoints(std::string_view utf8);

  // Decode a string literal to a single codepoint at compile time.
  // Requires a single, valid, canonical UTF-8 sequence.
  // TODO: Make this consteval when compilers are ready.
  static inline constexpr uint32_t Codepoint(std::string_view sv);

  // Get the number of codepoints in the UTF-8 string, assuming it is
  // encoded correctly. Doesn't handle stuff like combining
  // characters, but it's better than strlen if the string is not pure
  // ASCII.
  static inline size_t Length(std::string_view utf8);

  // Truncate to at most the given number of codepoints.
  static inline std::string Truncate(std::string_view utf8, int max_length);

  // Convert up to len bytes of utf8 data to get one codepoint. There
  // must be at least 1 byte in the input. Return the number of bytes
  // read and the codepoint. If the data are invalid, reads one byte
  // and returns 0xFFFFFFFF, which is an invalid codepoint.
  static inline std::pair<int, uint32_t> ParsePrefix(const char *utf8, int len);
  static constexpr uint32_t INVALID = 0xFFFFFFFF;

  // Like the previous, but modifying the view to consume the bytes. Returns
  // INVALID if the encoding is not valid or there are not enough bytes (including
  // an empty string).
  static inline uint32_t ConsumePrefix(std::string_view *utf8);

  // True if the string is valid UTF-8.
  static inline bool IsValid(std::string_view bytes);

  // Iterate over the codepoints in a string without allocating a copy.
  // You can do:
  /*
    for (uint32_t codepoint : UTF8::Decoder(s)) {
      ...
    }
  */
  struct Decoder {
    inline Decoder(std::string_view s);

    struct const_iterator {
      constexpr const_iterator(const char *ptr, const char *limit) :
        ptr(ptr), limit(limit) {}
      constexpr const_iterator(const const_iterator &other) = default;
      inline constexpr bool operator ==(const const_iterator &other) const;
      inline constexpr bool operator !=(const const_iterator &other) const;
      inline const_iterator &operator ++();
      inline const_iterator operator ++(int postfix);
      inline uint32_t operator *() const;

     private:
      const char *ptr = nullptr;
      const char *limit = nullptr;
    };

    constexpr const_iterator begin() const {
      return begin_it;
    }

    constexpr const_iterator end() const {
      return end_it;
    }

   private:
    const const_iterator begin_it, end_it;
  };

 private:
  // Helper used for Codepoint(); could make sense to expose it.
  static inline constexpr std::optional<uint32_t> DecodeOpt(std::string_view sv);
};



// Implementations follow.

constexpr std::string UTF8::Encode(uint32_t codepoint) {
  if (codepoint < 0x80) {
    std::string s;
    s.resize(1);
    s[0] = (uint8_t)codepoint;
    return s;
  } else if (codepoint < 0x800) {
    std::string s;
    s.resize(2);
    s[0] = 0xC0 | (codepoint >> 6);
    s[1] = 0x80 | (codepoint & 0x3F);
    return s;
  } else if (codepoint < 0x10000) {
    std::string s;
    s.resize(3);
    s[0] = 0xE0 | (codepoint >> 12);
    s[1] = 0x80 | ((codepoint >> 6) & 0x3F);
    s[2] = 0x80 | (codepoint & 0x3F);
    return s;
  } else if (codepoint < 0x110000) {
    std::string s;
    s.resize(4);
    s[0] = 0xF0 | (codepoint >> 18);
    s[1] = 0x80 | ((codepoint >> 12) & 0x3F);
    s[2] = 0x80 | ((codepoint >> 6) & 0x3F);
    s[3] = 0x80 | (codepoint & 0x3F);
    return s;
  }
  return "";
}

size_t UTF8::Length(std::string_view utf8) {
  size_t len = 0;
  for (size_t i = 0; i < utf8.size(); i++) {
    uint8_t c = utf8[i];
    // valid sequences are
    // 0xxxxxxx
    // 110xxxxx 10xxxxxx
    // 1110xxxx 10xxxxxx 10xxxxxx
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (c & 0x80) {
      // high bit set.
      if ((c & 0b11100000) == 0b11000000) {
        // two-byte sequence.
        // we just skip the next byte because we're not validating.
        i++;
      } else if ((c & 0b11110000) == 0b11100000) {
        // three bytes
        i += 2;
      } else if ((c & 0b11111000) == 0b11110000) {
        i += 3;
      }
    } else {
      // ASCII
    }
    len++;
  }
  return len;
}

std::vector<uint32_t> UTF8::Codepoints(std::string_view utf8) {
  std::vector<uint32_t> ret;
  ret.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size(); i++) {
    uint8_t c = utf8[i];
    // valid sequences are
    // 0xxxxxxx
    // 110xxxxx 10xxxxxx
    // 1110xxxx 10xxxxxx 10xxxxxx
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (c & 0x80) {
      // high bit set.
      if ((c & 0b11100000) == 0b11000000) {
        // two-byte sequence.
        if (i + 1 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];

        // 110xxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00011111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else if ((c & 0b11110000) == 0b11100000) {
        // three bytes
        if (i + 2 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];
        uint8_t e = utf8[++i];

        // 1110xxxx 10xxxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000 &&
            (e & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00001111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          cp <<= 6;
          cp |= (e & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else if ((c & 0b11111000) == 0b11110000) {
        // four bytes
        if (i + 3 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];
        uint8_t e = utf8[++i];
        uint8_t f = utf8[++i];

        // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000 &&
            (e & 0b11000000) == 0b10000000 &&
            (f & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00000111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          cp <<= 6;
          cp |= (e & 0b00111111);
          cp <<= 6;
          cp |= (f & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else {
        // If the broken encoding is multi-byte, there might be a
        // better choice than inserting multiple replacement chars,
        // but for now this is simplest.
        ret.push_back(REPLACEMENT_CODEPOINT);
      }
    } else {
      // ASCII
      ret.push_back(c);
    }
  }

  return ret;
}


std::string UTF8::Truncate(std::string_view utf8, int max_length) {
  std::vector<uint32_t> codepoints = Codepoints(utf8);
  if ((int)codepoints.size() > max_length) {
    codepoints.resize(max_length);
    std::string ret;
    ret.reserve(utf8.size());
    for (uint32_t cp : codepoints) {
      ret += Encode(cp);
    }
    return ret;
  } else {
    return std::string(utf8);
  }
}

std::pair<int, uint32_t> UTF8::ParsePrefix(const char *utf8, int buffer_len) {
  // Always need at least one byte!
  // This is a precondition of the function, but at least don't crash.
  if (buffer_len <= 0) return std::make_pair(1, INVALID);

  uint32_t ch = (uint8_t)utf8[0];
  int enc_len = 0;
  if ((ch & 0x80) == 0) {
    return std::make_pair(1, ch);
  } else if ((ch & 0xe0) == 0xc0 && buffer_len >= 2) {
    ch = ((uint32_t)ch & 0x1f) << 6;
    enc_len = 2;

  } else if ((ch & 0xf0) == 0xe0 && buffer_len >= 3) {
    ch = ((uint32_t)ch & 0x0f) << 12;
    enc_len = 3;

  } else if ((ch & 0xf8) == 0xf0 && buffer_len >= 4) {
    ch = ((uint32_t)ch & 0x7) << 18;
    enc_len = 4;

  } else {
    return std::make_pair(1, INVALID);
  }

  // Process (only) the continuation bytes.
  for (int i = 1; i < enc_len; i++) {
    const uint8_t b = (uint8_t)utf8[i];
    int shift = (enc_len - i - 1) * 6;
    // The continuation byte must be of the form 0b10xxxxxx.
    if ((b & 0b11000000) != 0b10000000) {
      return std::make_pair(1, INVALID);
    } else {
      ch |= ((uint32_t)b & 0x3f) << shift;
    }
  }

  return std::make_pair(enc_len, ch);
}

inline uint32_t UTF8::ConsumePrefix(std::string_view *utf8) {
  if (utf8->empty()) return INVALID;
  const auto &[len, cp] = ParsePrefix(utf8->data(), utf8->size());
  utf8->remove_prefix(len);
  return cp;
}


constexpr std::optional<uint32_t> UTF8::DecodeOpt(std::string_view sv) {
  if (sv.empty()) {
    return std::nullopt;
  }

  const uint8_t b1 = (uint8_t)sv[0];
  uint32_t cp = 0;
  size_t len = 0;
  uint32_t min_cp = 0;

  // Get expected length.
  if ((b1 & 0x80) == 0) { // 0xxxxxxx
    len = 1;
    cp = b1;
    min_cp = 0;
  } else if ((b1 & 0xE0) == 0xC0) { // 110xxxxx
    len = 2;
    cp = b1 & 0x1F;
    min_cp = 0x80;
  } else if ((b1 & 0xF0) == 0xE0) { // 1110xxxx
    len = 3;
    cp = b1 & 0x0F;
    min_cp = 0x800;
  } else if ((b1 & 0xF8) == 0xF0) { // 11110xxx
    len = 4;
    cp = b1 & 0x07;
    min_cp = 0x10000;
  } else {
    // Invalid (e.g., 10xxxxxx or 11111xxx)
    return std::nullopt;
  }

  if (sv.size() != (size_t)len) {
    return std::nullopt;
  }

  // Loop to process continuation bytes (10xxxxxx).
  for (size_t i = 1; i < len; i++) {
    const uint8_t byte = (uint8_t)sv[i];
    if ((byte & 0xC0) != 0x80) {
      // Invalid continuation byte.
      return std::nullopt;
    }
    cp = (cp << 6) | (byte & 0x3F);
  }

  if (cp < min_cp) {
    // Overlong (non-canonical) encoding.
    return std::nullopt;
  }

  if (cp > 0x10FFFF) {
    return std::nullopt;
  }

  if (cp >= 0xD800 && cp <= 0xDFFF) {
    // A UTF-16 surrogate.
    return std::nullopt;
  }

  return cp;
}

constexpr uint32_t UTF8::Codepoint(std::string_view sv) {
  std::optional<uint32_t> co = DecodeOpt(sv);
  if (!co.has_value()) {
    throw "Invalid UTF-8 sequence provided to UTF8::Codepoint";
  }
  return co.value();
}

UTF8::Decoder::Decoder(std::string_view s) :
  begin_it(s.data(), s.data() + s.size()),
  end_it(s.data() + s.size(), s.data() + s.size()) {}


constexpr bool UTF8::Decoder::const_iterator::operator ==(
    const const_iterator &other) const {
  return other.ptr == ptr;
}

constexpr bool UTF8::Decoder::const_iterator::operator !=(
    const const_iterator &other) const {
  return other.ptr != ptr;
}


UTF8::Decoder::const_iterator &UTF8::Decoder::const_iterator::operator ++() {
  // prefix.
  const auto &[code_len, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  assert(code != UTF8::INVALID);
  ptr += code_len;
  return *this;
}

UTF8::Decoder::const_iterator
UTF8::Decoder::const_iterator::operator ++(int postfix) {
  auto old = *this;
  const auto &[code_len, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  assert(code != UTF8::INVALID);
  ptr += code_len;
  return old;
}

uint32_t UTF8::Decoder::const_iterator::operator *() const {
  const auto &[code_len_, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  return code;
}


bool UTF8::IsValid(std::string_view bytes) {
  while (!bytes.empty())
    if (ConsumePrefix(&bytes) == INVALID)
      return false;

  return true;
}


#endif

