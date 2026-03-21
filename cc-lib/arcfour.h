
/* Alleged RC4 algorithm.
   The RC4 name is trademarked by RSA DSI.
   This implementation is based on the algorithm
   published in Applied Cryptography.

   This algorithm is adorably simple, but
   should only be used for cryptography with
   significant care. Note also that like many
   pseudorandom number generators, there are
   some small biases in its output statistics.
*/

#ifndef _CC_LIB_ARCFOUR_H
#define _CC_LIB_ARCFOUR_H

#include <string>
#include <vector>
#include <cstdint>

struct ArcFour {
  using uint8 = uint8_t;

  explicit ArcFour(const std::vector<uint8> &v);
  explicit ArcFour(const std::string &s);
  // Unspecified initialization (and of course 64 bits
  // is not enough entropy to be secure).
  explicit ArcFour(uint64_t seed);

  // Get the next byte.
  uint8 Byte();
  inline uint64_t Word64();

  // Discard n bytes from the stream. It is
  // strongly recommended that new uses of
  // arcfour discard at least 1024 bytes after
  // initialization, to prevent against the
  // 2001 attack by Fluhrer, Mantin, and Shamir.
  void Discard(int n);

 private:
  uint8 ii, jj;
  uint8 ss[256];
};


// Implementations follow.

uint64_t ArcFour::Word64() {
  uint64_t w = 0;
  for (int i= 0; i < 8; i++) {
    w <<= 8;
    w |= Byte();
  }
  return w;
}


#endif
