
#include "arcfour.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

using uint8 = uint8_t;
static_assert (sizeof(uint8) == 1, "Char must be one byte.");

template<class K>
static void Initialize(K k,
                       uint8 (&ss)[256]) {
  uint8 j = 0;
  for (int i = 0; i < 256; i++) ss[i] = i;
  for (int i = 0; i < 256; i++) {
    j += ss[i] + k(i);
    uint8 t = ss[i];
    ss[i] = ss[j];
    ss[j] = t;
  }
}

ArcFour::ArcFour(const vector<uint8> &v) : ii(0), jj(0) {
  Initialize([&v](int i) { return v[i % v.size()]; }, ss);
}

ArcFour::ArcFour(const string &s) : ii(0), jj(0) {
  Initialize([&s](int i) { return s[i % s.size()]; }, ss);
}

ArcFour::ArcFour(uint64_t seed) : ii(0), jj(0) {
  Initialize([seed](int i) { return 0xFF & (seed >> ((i % 8) * 8)); }, ss);
}

uint8_t ArcFour::Byte() {
  ii++;
  jj += ss[ii];
  uint8_t ti = ss[ii];
  uint8_t tj = ss[jj];
  ss[ii] = tj;
  ss[jj] = ti;
  return ss[(ti + tj) & 255];
}

void ArcFour::Discard(int n) {
  while (n--) (void)Byte();
}


