
#include <functional>
#ifdef __APPLE__
// fstat64 is deprecated; force fstat to be 64-bit
#define _DARWIN_USE_64_BIT_INODE 1
#endif

#include "util.h"

#include <algorithm>
#include <bit>
#include <bitset>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/print.h"

// Note: It is a design goal for this to only depend on the standard
// library (not even base/*)!

#if defined(WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
   /* chdir */
#  include <direct.h>
   /* getpid */
#  include <process.h>
   /* time */
#  include <time.h>
   /* rename */
#  include <io.h>

// For C++17, avoid conflicts with std::byte
#  define byte win_byte_override
#  include <windows.h>
#  undef byte

#  if defined(__MINGW32__) || defined(__MINGW64__)
#    include <dirent.h>
#  endif

// Visual studio only.
#  if defined(WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
#    pragma warning(disable: 4996)
#  endif

#else /* posix */
   /* chdir, unlink */
#  include <unistd.h>
   /* getpid */
#  include <sys/types.h>
   /* isalnum */
#  include <ctype.h>
   /* directory stuff */
#  include <dirent.h>
#endif

#ifdef __APPLE__
// fstat64 is deprecated; force fstat to be 64-bit
#  define stat64 stat
#  define fstat64 fstat
#elif defined(_MSC_VER)
// MSVC uses underscored names
#  define stat64 _stat64
#  define fstat64 _fstat64
#endif

#if defined(WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
static constexpr inline bool IsDirSep(char c) {
  return c == '/' || c == '\\';
}
#else
static constexpr inline bool IsDirSep(char c) {
  return c == '/';
}
#endif


using namespace std;

using uint8 = uint8_t;
using int64 = int64_t;
using uint64 = uint64_t;

// TODO: This is used internally, but only for tempfile.
// We should just use mkstmp, which also avoids races.
static int Random();
namespace {
struct RandomSeeder {
  RandomSeeder() {
# if defined(WIN32) || defined(__MINGW32__)
    srand((int)time(nullptr) ^ getpid());
# else
    srandom(time(0) ^ getpid());
# endif
  }
};
}

static int Random() {
  // Run exactly once, with initialization thread safe.
  // Result is not used.
  static RandomSeeder *unused = new RandomSeeder;
  (void)unused;
# if defined(WIN32) || defined(__MINGW32__)
  return ::rand();
# else
  return ::random();
# endif
}

string Util::itos(int i) {
  return std::format("{}", i);
}

int Util::stoi(const string &s) {
  return atoi(s.c_str());
}

// TODO: I never tested this on posix.
vector<string> Util::ListFiles(const string &s) {
  vector<string> v;
#if defined(WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
  // MSVC: use std::filesystem since dirent.h is not available
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(s, ec)) {
    string name = entry.path().filename().string();
    if (name != "." && name != "..") {
      v.push_back(std::move(name));
    }
  }
#else
  DIR *dir = opendir(s.c_str());
  if (dir == nullptr) return {};
  while (struct dirent *res = readdir(dir)) {
    string s = res->d_name;
    if (s != "." && s != "..") {
      v.push_back(std::move(s));
    }
  }
  closedir(dir);
#endif
  return v;
}

bool Util::IsHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
    ((c | 32) >= 'a' && (c | 32) <= 'f');
}
int Util::HexDigitValue(char c) {
  // One weird trick.
  return ((int)c | 4400) % 55;
}

char Util::HexDigit(int v) {
  return "0123456789abcdef"[v & 0xF];
}

std::string Util::HexString(std::string_view s,
                            const char *sep_ptr,
                            const char *prefix_ptr) {
  string sep = sep_ptr ? (string)sep_ptr : "";
  string pfx = prefix_ptr ? (string)prefix_ptr : "";

  if (s.empty()) return "";

  string out;
  out.reserve(s.size() * (2 + pfx.size()) + (s.size() - 1) * sep.size());
  for (int i = 0; i < (int)s.size(); i++) {
    char c = s[i];
    if (i != 0) out += sep;
    out += pfx;
    out += HexDigit((c >> 4) & 0xF);
    out += HexDigit(c & 0xF);
  }
  return out;
}

std::optional<uint64_t> Util::ParseBinary(std::string_view s) {
  if (s.empty()) return std::nullopt;
  if (s.size() > 64) return std::nullopt;

  uint64_t out = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c != '0' && c != '1') return std::nullopt;
    out <<= 1;
    out |= (c - '0');
  }

  return {out};
}

std::optional<uint64_t> Util::ParseHex(std::string_view s) {
  if (s.empty()) return std::nullopt;
  if (s.size() > 16) return std::nullopt;

  uint64_t out = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (!IsHexDigit(c)) return std::nullopt;
    out <<= 4;
    out |= HexDigitValue(c);
  }

  return {out};
}

bool Util::isdir(std::string_view filename) {
  struct stat st;
  std::string f{filename};
  return (0 == stat(f.c_str(), &st)) && (st.st_mode & S_IFDIR);
}

bool Util::ExistsFile(std::string_view filename) {
  struct stat st;
  std::string s{filename};
  return 0 == stat(s.c_str(), &st);
}

bool Util::existsdir(const string &d) {
  return isdir(d); /* (ExistsFile(d) && isdir(d.c_str())); */
}

/* XXX what mode? */
bool Util::MakeDir(const string &d) {
# if defined(WIN32) || defined(__MINGW32__)
  return !mkdir(d.c_str());
# else /* posix */
  return !mkdir(d.c_str(), 0755);
# endif
}

std::string Util::ReadStdin() {
  constexpr size_t BUFFER_SIZE = 4096;
  // PERF: Use uninitialized resize.
  std::string content(BUFFER_SIZE, 0);
  size_t offset = 0;

  // 2. Read from stdin in chunks until EOF is reached.
  for (;;) {
    assert(content.size() >= offset);
    // Make space to read at least BUFFER_SIZE bytes.
    while (content.size() - offset < BUFFER_SIZE) {
      // PERF: Uninitialized resize.
      content.resize(std::max((size_t)(content.size() * 1.5), content.size() + BUFFER_SIZE));
    }

    size_t bytes_read = std::fread(content.data() + offset, 1, BUFFER_SIZE, stdin);
    offset += bytes_read;
    // Upon any short read, we are done. We don't distinguish errors from EOF.
    if (bytes_read < BUFFER_SIZE) {
      content.resize(offset);
      content.shrink_to_fit();
      return content;
    }
  }
}

// Internal helper used by ReadFile, ReadFileMagic, ReadFileBytes.
// T is string or vector<uint8>.
// If magic_opt is non-null, it is copied to the start of the
// container we return.
template<class T>
static T ReadAndCloseFile(FILE *f, const T *magic_opt) {
  #define READFILE_DEBUG 0
  // This is unbelievably difficult!
  // A simple loop while getc() doesn't return EOF works, but
  // is much slower than it needs to be.
  // fseek(.., SEEK_END) is useless because it has undefined
  // behavior (!) despite "usually" working.
  // stat() returns a size, but it is bogus for special files,
  // like the ones in /proc. It should be accurate for regular
  // files.
  // Therefore, what we want to do here is use stat() to
  // estimate the file size, and be efficient in the case that
  // we got it right. However, we need to remain correct even
  // when stat returns nonsense (such as 0 or 4096).

  // TODO: Not sure how portable fstat64 is. OS X marks it as
  // deprecated since 10.6, and we define it to be the equivalent
  // "stat" after forcing it to use 64-bit inodes
  // (_DARWIN_USE_64_BIT_INODE). On other platforms, an alternative is
  // to only do gigabyte-size reads, although reading a file that's a
  // significant fraction of all available memory is one of the main
  // points of going through all this complexity!
  int fd = fileno(f);
  struct stat64 st;
  if (0 != fstat64(fd, &st)) {
    fclose(f);
    return {};
  }

  #if READFILE_DEBUG
  Print("size_t is {} bytes, signed: {}\n",
        sizeof (size_t), std::is_signed<size_t>::value ? "yes" : "no");
  Print("stat {}:\n"
        "  st_size: {}\n (off_t is {} bytes, signed: {})\n",
        fd, (uint64)st.st_size, sizeof (st.st_size),
        std::is_signed<decltype (st.st_size)>::value ? "yes" : "no");
  #endif

  T ret;
  int64 next_pos = 0;
  int64 size_guess = st.st_size;
  if (magic_opt != nullptr) {
    ret = *magic_opt;
    next_pos = ret.size();
  }

  // On cygwin for huge files, we get the size correct and read all
  // the bytes in a single call to fread. But feof doesn't return true
  // right after this, which causes us to do a useless resize.
  // Instead, just attempt to read one additional byte, which causes
  // fread to recognize the EOF. (This assumes that resizing the
  // string downward by one byte at the end is basically free.)
  size_guess++;

  #if READFILE_DEBUG
  Print("next_pos is {}; size_guess is {}.\n",
        next_pos, size_guess);
  #endif

  // In optimistic cases where the size_guess is correct,
  // this loop will execute just once: A single resize, and
  // a single fread.
  for (;;) {
    // Now, repeatedly, resize the string to accommodate a
    // read that we think will finish off the file. But
    // don't do zero-sized writes!
    if (next_pos >= size_guess) {
      // XXX possibility for overflow, ugh
      size_guess = next_pos + 16;
    }
    #if READFILE_DEBUG
    Print("Resize buffer to {}\n", size_guess);
    #endif

    // Keep the buffer large enough to store what we think the actual
    // size is, so that we don't have to keep resizing it.
    if ((int64)ret.size() < size_guess) {
      #if READFILE_DEBUG
      Print("Resize buffer {} -> {}\n", (int64)ret.size(), size_guess);
      #endif
      ret.resize(size_guess);
    } else {
      #if READFILE_DEBUG
      Print("Buffer sized {}; already big enough for guess {}\n",
            (int64)ret.size(), size_guess);
      #endif
    }

    // TODO: Tune this for various platforms to find a good
    // compromise. Could even be platform dependent. For corretness,
    // this should not be any larger than 2^31 - 1 (or so), since for
    // example on mingw, fread just fails for such sizes. Huge reads
    // seem to be pretty slow (perhaps preventing scheduling of other
    // threads).
    static constexpr int64 MAX_READ_SIZE = 1LL << 24;
    static_assert(MAX_READ_SIZE < ((1LL << 31) - 1),
                  "Must fit in signed 32-bit gamut.");

    const int64 read_size =
      std::min(size_guess - next_pos, MAX_READ_SIZE);
    #if READFILE_DEBUG
    Print("Attempt to read {} bytes\n", read_size);
    #endif

    // Bytes are required to be contiguous from C++11;
    // use .front() instead of [next_pos] since the former,
    // introduced in C++11, will prove we have a compatible
    // version.
    const size_t bytes_read =
      fread(&ret.front() + next_pos, 1, read_size, f);
    #if READFILE_DEBUG
    Print("{} bytes were read\n", (int64)bytes_read);
    #endif

    // We read exactly this many bytes.
    next_pos += bytes_read;
    #if READFILE_DEBUG
    Print("Now next_pos is {}\n", next_pos);
    #endif
    if (feof(f)) {
      #if READFILE_DEBUG
      Print("EOF. current ret size is {}; resizing to {}\n",
            (int64)ret.size(), next_pos);
      #endif
      // Should be no-op when we guessed correctly.
      ret.resize(next_pos);
      fclose(f);
      return ret;
    }

    // If we're not at the end of file but no bytes were
    // read, then something is amiss. This also ensures
    // that the loop makes progress.
    if (bytes_read == 0) {
      #if READFILE_DEBUG
      Print("No bytes read but not EOF?\n");
      #endif
      fclose(f);
      return {};
    }
  }
}

std::optional<string> Util::ReadFileOpt(std::string_view sv) {
  std::string s{sv};
  if (Util::isdir(s)) return nullopt;
  if (s.empty()) return nullopt;
  FILE *f = fopen(s.c_str(), "rb");
  if (f == nullptr) return nullopt;

  // TODO: Some failures are possible in here; should return nullopt for
  // those as well.
  // TODO PERF: Make sure we aren't unnecessarily copying here?
  return {ReadAndCloseFile<string>(f, nullptr)};
}

string Util::ReadFile(std::string_view sv) {
  std::string s{sv};
  if (Util::isdir(s)) return "";
  if (s == "") return "";

  FILE *f = fopen(s.c_str(), "rb");
  if (!f) return "";
  return ReadAndCloseFile<string>(f, nullptr);
}

// PERF: Benchmark against ForEachLine approach.
// XXX: Probably this should return an empty vector if the
// file does not exist?
vector<string> Util::ReadFileToLines(std::string_view filename) {
  return SplitToLines(ReadFile(filename));
}

vector<string> Util::NormalizeLines(std::span<const string> lines) {
  std::vector<string> out;
  out.reserve(lines.size());
  for (const string &line : lines) {
    string nl = NormalizeWhitespace(line);
    if (!nl.empty()) out.push_back(std::move(nl));
  }
  return out;
}

bool Util::WriteLinesToFile(std::string_view sv,
                            std::span<const string> lines) {
  std::string filename{sv};
  FILE *f = fopen(filename.c_str(), "wb");
  if (f == nullptr) return false;

  for (const string &s : lines) {
    size_t len = s.size();
    size_t wrote_len = fwrite(s.c_str(), 1, len, f);
    if (len != wrote_len) {
      fclose(f);
      return false;
    }
    if (EOF == fputc('\n', f)) {
      fclose(f);
      return false;
    }
  }

  fclose(f);

  return true;
}

vector<string> Util::SplitToLines(string_view s) {
  vector<string> v;
  string line;
  // PERF don't need to do so much copying.
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') {
      continue;
    } else if (s[i] == '\n') {
      v.push_back(std::move(line));
      line.clear();
    } else {
      line += s[i];
    }
  }
  if (!line.empty()) v.push_back(std::move(line));
  return v;
}

std::map<string, string> Util::ReadFileToMap(string_view filename) {
  std::map<string, string> m;
  vector<string> lines = ReadFileToLines(filename);
  for (int i = 0; i < (int)lines.size(); i++) {
    string rest = lines[i];
    string tok = chop(rest);
    rest = LoseWhiteL(rest);
    m.insert(make_pair(tok, rest));
  }
  return m;
}

static inline string PadWith(int n, string s, char c) {
  if (n >= 0) {
    if ((int)s.length() < n) {
      s.reserve(n);
      while ((int)s.length() < n)
        s.push_back(c);
    }
    return s;
  } else {
    // n < 0. Pad left, but negate n so it's easier to think about.
    n = -n;
    if ((int)s.length() < n) {
      string ret(n - s.length(), c);
      ret.append(s);
      return ret;
    } else {
      return s;
    }
  }
}

string Util::Pad(int n, string s) {
  return PadWith(n, std::move(s), ' ');
}

string Util::PadEx(int n, string s, char c) {
  return PadWith(n, std::move(s), c);
}

std::optional<std::vector<uint8_t>>
Util::UnescapeC(std::string_view str) {
  std::vector<uint8_t> out;
  out.reserve(str.size());
  while (!str.empty()) {
    char c = str[0];
    str.remove_prefix(1);
    if (c == '\\') {
      if (str.empty()) return std::nullopt;

      char d = str[0];
      str.remove_prefix(1);
      switch (d) {
      case '\\':
      case '\"':
      case '\'':
        out.push_back(d);
        break;
      case '0':
        out.push_back(0);
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'x': {
        uint32_t val = 0;
        bool had_char = false;
        while (!str.empty() &&
               IsHexDigit(str[0])) {
          val *= 0x10;
          val += HexDigitValue(str[0]);
          str.remove_prefix(1);
          had_char = true;
          if (val > 0xFF) {
            return std::nullopt;
          }
        }

        if (!had_char)
          return std::nullopt;

        out.push_back(val);
        break;
      }

      default:
        return std::nullopt;
      }

    } else {
      out.push_back(c);
    }
  }

  return {out};
}


vector<uint8> Util::ReadFileBytes(std::string_view filename) {
  if (Util::isdir(filename)) return {};
  if (filename.empty()) return {};

  std::string s{filename};
  FILE *f = fopen(s.c_str(), "rb");
  if (!f) return {};
  return ReadAndCloseFile<vector<uint8>>(f, nullptr);
}


static bool HasMagicF(FILE *f, std::string_view mag) {
  char *hdr = (char*)malloc(mag.length());
  if (!hdr) return false;

  /* we may not even be able to read sizeof(header) bytes! */
  if (mag.length() != fread(hdr, 1, mag.length(), f)) {
    free(hdr);
    return false;
  }

  for (unsigned int i = 0; i < mag.length(); i++) {
    if (hdr[i] != mag[i]) {
      free(hdr);
      return false;
    }
  }

  free(hdr);
  return true;
}

bool Util::HasMagic(std::string_view s, std::string_view mag) {
  FILE *f = fopen(std::string(s).c_str(), "rb");
  if (!f) return false;

  bool hm = HasMagicF(f, mag);

  fclose(f);
  return hm;
}

string Util::ReadFileMagic(std::string_view s, std::string_view mag) {
  if (isdir(s)) return "";
  if (s == "") return "";

  FILE *f = fopen(std::string(s).c_str(), "rb");

  if (!f) return "";


  if (!HasMagicF(f, mag)) {
    fclose(f);
    return "";
  }

  // OK, now just read file.
  std::string mag_string{mag};
  return ReadAndCloseFile<std::string>(f, &mag_string);
}

bool Util::WriteFile(std::string_view fn, std::string_view s) {
  FILE *f = fopen(std::string(fn).c_str(), "wb");
  if (f == nullptr) return false;

  const size_t len = s.size();
  const size_t wrote_len = fwrite(s.data(), 1, len, f);

  fclose(f);

  return len == wrote_len;
}

bool Util::WriteFileBytes(std::string_view filename,
                          std::span<const uint8> bytes) {
  std::string fn{filename};
  FILE *f = fopen(fn.c_str(), "wb");
  if (!f) return false;

  const size_t len = bytes.size();
  const size_t wrote_len = fwrite(&bytes[0], 1, len, f);

  fclose(f);

  return len == wrote_len;
}

vector<uint64> Util::ReadUint64File(std::string_view filename) {
  vector<uint8> bytes = ReadFileBytes(filename);
  if (bytes.size() & 7) return {};
  vector<uint64> ret;
  ret.reserve(bytes.size() / 8);
  uint64 w = 0ULL;
  for (int i = 0; i < (int)bytes.size(); i++) {
    w <<= 8;
    w |= bytes[i];
    if ((i & 7) == 7) {
      ret.push_back(w);
      w = 0ULL;
    }
  }
  return ret;
}

bool Util::WriteUint64File(std::string_view filename,
                           std::span<const uint64_t> contents) {
  vector<uint8> bytes;
  bytes.reserve(contents.size() * 8);
  for (uint64 w : contents) {
    bytes.push_back(0xFF & (w >> 56));
    bytes.push_back(0xFF & (w >> 48));
    bytes.push_back(0xFF & (w >> 40));
    bytes.push_back(0xFF & (w >> 32));
    bytes.push_back(0xFF & (w >> 24));
    bytes.push_back(0xFF & (w >> 16));
    bytes.push_back(0xFF & (w >> 8));
    bytes.push_back(0xFF & (w));
  }
  return WriteFileBytes(filename, bytes);
}

unsigned int Util::hash(const string &s) {
  unsigned int h = 0x714FA5DD;
  for (unsigned int i = 0; i < s.length(); i ++) {
    h = (h << 11) | (h >> (32 - 11));
    h *= 3113;
    h ^= (unsigned char)s[i];
  }
  return h;
}

string Util::lcase(std::string_view in) {
  string out;
  out.reserve(in.size());
  for (unsigned int i = 0; i < in.length(); i++) {
    if (in[i] >= 'A' &&
        in[i] <= 'Z') out += in[i]|32;

    else out += in[i];
  }
  return out;
}

string Util::ucase(std::string_view in) {
  string out;
  out.reserve(in.size());
  for (int64_t i = 0; i < (int64_t)in.length(); i++) {
    if (in[i] >= 'a' &&
        in[i] <= 'z') out += (in[i] & (~ 32));

    else out += in[i];
  }
  return out;
}

string Util::FileOf(string_view s) {
  for (int64_t i = s.length() - 1; i >= 0; i--) {
    if (IsDirSep(s[i])) {
      return std::string(s.substr(i + 1, s.length() - (i + 1)));
    }
  }
  return std::string(s);
}

// TODO: Use stdlib for this; it looks good now.
// On windows the situation is very confusing because you will see
// both / and \ in practice.
string Util::PathOf(string_view s) {
  if (s.empty()) return ".";
  for (int64_t i = s.length() - 1; i >= 0; i--) {
    if (IsDirSep(s[i])) {
      return std::string(s.substr(0, i + 1));
    }
  }
  return ".";
}

std::string_view Util::FileExtOf(std::string_view s) {
  auto pos = s.rfind('.');
  if (pos == std::string_view::npos) return {};
  return s.substr(pos + 1, std::string_view::npos);
}

std::string_view Util::FileBaseOf(std::string_view s) {
  auto pos = s.rfind('.');
  if (pos == std::string_view::npos) return s;
  return s.substr(0, pos);
}

/* XX can use EndsWith below */
string Util::ensureext(string f, string ext) {
  if (f.length() < ext.length())
    return f + ext;
  else {
    if (f.substr(f.length() - ext.length(),
                 ext.length()) != ext)
      return f + ext;
    else return f;
  }
}

bool Util::EndsWith(string_view big, string_view little) {
  // In c++20, can use s->ends_with(suffix).
  if (big.size() < little.size()) return false;
  return big.substr(big.size() - little.size()) == little;
}

bool Util::StartsWith(string_view big, string_view little) {
  // In c++20, can use s->starts_with(suffix).
  if (big.size() < little.size()) return false;
  return big.substr(0, little.size()) == little;
}

bool Util::TryStripSuffix(string_view suffix, string_view *s) {
  if (EndsWith(*s, suffix)) {
    s->remove_suffix(suffix.length());
    return true;
  }
  return false;
}

bool Util::TryStripSuffix(string_view suffix, string *s) {
  if (EndsWith(*s, suffix)) {
    s->resize(s->size() - suffix.length());
    return true;
  }
  return false;
}

bool Util::TryStripPrefix(string_view prefix, string_view *s) {
  if (StartsWith(*s, prefix)) {
    s->remove_prefix(prefix.length());
    return true;
  }
  return false;
}

bool Util::TryStripPrefix(string_view prefix, string *s) {
  if (StartsWith(*s, prefix)) {
    *s = s->substr(prefix.length(), string::npos);
    return true;
  }
  return false;
}

bool Util::StrContains(string_view haystack, string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

int Util::changedir(string s) {
  return !chdir(s.c_str());
}

int Util::getpid() {
  return ::getpid();
}

string Util::getline(string &chunk) {
  string ret;
  for (unsigned int i = 0; i < chunk.length(); i ++) {
    if (chunk[i] == '\r') continue;
    else if (chunk[i] == '\n') {
      chunk = chunk.substr(i + 1, chunk.length() - (i + 1));
      return ret;
    } else ret += chunk[i];
  }
  /* there doesn't need to be a final trailing newline. */
  chunk = "";
  return ret;
}

/* PERF */
string Util::fgetline(FILE *f) {
  string out;
  int c;
  while ( (c = fgetc(f)), ((c != EOF) && (c != '\n')) ) {
    /* ignore CR */
    if (c != '\r') {
      out += (char)c;
    }
  }
  return out;
}

/* PERF use substr instead of accumulating: this is used
   frequently in the net stuff */
// FIXME: this is documented as "whitespace" but only looks
// for spaces.
/* return first token in line, removing it from 'line' */
string Util::chop(string &line) {
  for (unsigned int i = 0; i < line.length(); i ++) {
    if (line[i] != ' ') {
      string acc;
      for (unsigned int j = i; j < line.length(); j ++) {
        if (line[j] == ' ') {
          line = line.substr(j, line.length() - j);
          return acc;
        } else acc += line[j];
      }
      line = "";
      return acc;
    }
  }
  /* all whitespace */
  line = "";
  return "";
}

std::string_view Util::Chop(std::string_view *line) {
  std::string_view ret = *line;

  // Remove prefix whitespace from both.
  while (!ret.empty() && ret[0] == ' ') {
    ret.remove_prefix(1);
    line->remove_prefix(1);
  }

  // Now find non-whitespace character.
  const auto pos = ret.find(' ');
  if (pos == std::string_view::npos) {
    // Whole string. Line becomes empty.
    line->remove_prefix(line->size());
    return ret;
  }

  ret = ret.substr(0, pos);
  line->remove_prefix(pos);
  return ret;
}

int64_t Util::ParseInt64(std::string_view s,
                         int64_t default_value) {
  RemoveOuterWhitespace(&s);
  int64_t ret = default_value;
  auto res = std::from_chars(s.data(), s.data() + s.size(), ret);
  // Must consume entire remaining string.
  if (res.ec == std::errc{} && res.ptr == s.data() + s.size()) {
    return ret;
  } else {
    return default_value;
  }
}

std::optional<int64_t> Util::ParseInt64Opt(std::string_view s) {
  RemoveOuterWhitespace(&s);
  int64_t ret = 0;
  auto res = std::from_chars(s.data(), s.data() + s.size(), ret);
  if (res.ec == std::errc{} && res.ptr == s.data() + s.size()) {
    return {ret};
  } else {
    return std::nullopt;
  }
}

optional<double> Util::ParseDoubleOpt(std::string_view s) {
  // To get rid of leading and trailing whitespace. strtod will skip
  // it anyway, but we want to be able to check that the whole
  // string was consumed in a simple way.

  // PERF: Do this without copying. from_chars supports floats.
  string ss = NormalizeWhitespace(s);
  char *endptr = nullptr;
  double d = strtod(ss.c_str(), &endptr);
  if (endptr == ss.c_str() + ss.size()) {
    return make_optional(d);
  } else {
    return nullopt;
  }
}

double Util::ParseDouble(std::string_view s, double default_value) {
  optional<double> od = ParseDoubleOpt(s);
  if (od.has_value()) return od.value();
  else return default_value;
}

/* PERF same */
string Util::ChopTo(char c, string &line) {
  string acc;
  for (unsigned int i = 0; i < line.length(); i ++) {
    if (line[i] != c) {
      acc += line[i];
    } else {
      if (i < (line.length() - 1)) {
        line = line.substr(i + 1, line.length() - (i + 1));
        return acc;
      } else {
        line = "";
        return acc;
      }
    }
  }
  /* character didn't appear; treat as an invisible
     occurrence at the end */
  line = "";
  return acc;
}

string Util::LoseWhiteL(const string &s) {
  for (unsigned int i = 0; i < s.length(); i ++) {
    switch(s[i]) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      /* keep going ... */
      break;
    default:
      return s.substr(i, s.length() - i);
    }
  }
  /* all whitespace */
  return "";
}

string Util::LoseWhiteR(string s) {
  while (!s.empty()) {
    switch (s.back()) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      s.resize(s.size() - 1);
      break;
    default:
      return s;
    }
  }
  // All whitespace.
  return "";
}

bool Util::IsWhitespace(char c) {
  switch (c) {
  case ' ':
  case '\n':
  case '\r':
  case '\t':
    return true;
  default:
    return false;
  }
}

string Util::RemoveChar(std::string_view s, char c) {
  return RemoveCharsMatching(s, [c](char cc) { return c == cc; });
}

void Util::RemoveOuterWhitespace(std::string_view *s) {
  while (!s->empty() && IsWhitespace((*s)[0]))
    s->remove_prefix(1);

  while (!s->empty() && IsWhitespace(s->back()))
    s->remove_suffix(1);
}

string Util::NormalizeWhitespace(std::string_view s) {
  string ret;
  // Skip at beginning.
  bool skip_ws = true;
  for (char c : s) {
    switch (c) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      if (skip_ws) continue;
      ret += ' ';
      skip_ws = true;
      break;
    default:
      skip_ws = false;
      ret += c;
    }
  }

  return LoseWhiteR(std::move(ret));
}

string Util::tempfile(const string &suffix) {
  static int tries = 0;

  std::string fname;

  do {
    fname = std::format("{}_{}_{}{}",
                        tries, getpid(), Random(),
                        suffix);
    tries++;
  } while (ExistsFile(fname));

  return fname;
}

/* break up the strings into tokens. A token is either
   a single character (non-numeral) or a sequence of
   numerals that are interpreted as a number. We then
   do lexicographic comparison on this stream of tokens.
   assumes ascii.

   l < r     -1
   l = r      0
   l > r      1

   XXX this treats

   abc 0123 def
   abc 123 def

   as equal strings. perhaps don't allow 0 to start a
   number?

   n.b. it is easy to overflow here, so perhaps comparing
   as we go is better
*/
int Util::natural_compare(const string &l, const string &r) {

  for (int caseless = 0; caseless < 2; caseless ++) {

    unsigned int il = 0;
    unsigned int ir = 0;

    while (il < l.length() || ir < r.length()) {
      /* if out of tokens in either string, it comes first. */
      if (il >= l.length()) return -1;
      if (ir >= r.length()) return 1;

      int lc = (unsigned char)l[il];
      int rc = (unsigned char)r[ir];

      if (lc >= '0' && lc <= '9') {
        if (rc >= '0' && rc <= '9') {
          /* compare ints */
          int ll = 0;
          int rr = 0;

          while (il < l.length() && l[il] >= '0' && l[il] <= '9') {
            ll *= 10;
            ll += (l[il] - '0');
            il ++;
          }

          while (ir < r.length() && r[ir] >= '0' && r[ir] <= '9') {
            rr *= 10;
            rr += (r[ir] - '0');
            ir ++;
          }

          if (ll < rr) return -1;
          if (ll > rr) return 1;
          /* otherwise continue... */

          il ++;
          ir ++;
        } else {
          /* treat numbers larger than any char. */
          return 1;
        }
      } else {
        if (rc >= '0' && rc <= '9') {
          return -1;
        } else {
          /* compare chars */
          if ((rc|32) >= 'a' && (rc|32) <= 'z' &&
              (lc|32) >= 'a' && (rc|32) <= 'z' &&
              !caseless) {

            /* letters are case-insensitive */
            if ((lc|32) < (rc|32)) return -1;
            if ((lc|32) > (rc|32)) return 1;
          } else {
            if (lc < rc) return -1;
            if (lc > rc) return 1;
          }

          /* same so far. continue... */

          il ++;
          ir ++;
        }
      }

    }
    /* strings look equal when compared
       as case-insensitive. so try again
       sensitive */
  }

  /* strings are case-sensitive equal! */

  return 0;
}

/* same as above, but ignore "the" at beginning */
/* XXX also ignore symbols ie ... at the beginning */
int Util::library_compare(const string &l, const string &r) {

  /* XXX currently IGNOREs symbols, which could give incorrect
     results for strings that are equal other than their
     leading punctuation */
  unsigned int idxl = 0;
  unsigned int idxr = 0;
  while (idxl < l.length() && (!isalnum(l[idxl]))) idxl++;
  while (idxr < r.length() && (!isalnum(r[idxr]))) idxr++;

  bool thel = false;
  bool ther = false;
  if (l.length() >= (5 + idxl) &&
      (l[idxl + 0]|32) == 't' &&
      (l[idxl + 1]|32) == 'h' &&
      (l[idxl + 2]|32) == 'e' &&
      (l[idxl + 3])    == ' ') thel = true;

  if (r.length() >= (5 + idxr) &&
      (r[idxr + 0]|32) == 't' &&
      (r[idxr + 1]|32) == 'h' &&
      (r[idxr + 2]|32) == 'e' &&
      (r[idxr + 3])    == ' ') ther = true;

  if (thel != ther) {
    if (thel) idxl += 4;
    else idxr += 4;
  }

  return natural_compare(l.substr(idxl, l.length() - idxl),
                         r.substr(idxr, r.length() - idxr));
}

string Util::UnsignedWithCommas(uint64_t u) {
  // PERF: too much copying!
  if (u == 0) return "0";
  string out;
  while (u) {
    int triple = u % 1000;
    u /= 1000;
    if (u) {
      out = std::format(",{:03d}{}", triple, out);
    } else {
      // no zero-padding, no comma
      out = std::format("{}{}", triple, out);
    }
  }
  return out;
}

bool Util::MatchSpec(std::string_view spec, char c) {
  if (spec.empty()) return false;
  bool match = true;
  if (spec[0] == '^') {
    spec.remove_prefix(1);
    match = !match;
  }

  // now loop looking for c in string, or ranges.
  for (size_t i = 0; i < spec.length(); i ++) {
    //  handle ranges, if it's not the first or last char.
    if (spec[i] == '-' && i > 0 && i < (spec.length() - 1)) {
      if (spec[i - 1] <= c &&
          spec[i + 1] >= c) return match;
      // skip dash and next char.
      i += 2;
    } else {
      // ok if starts range, since they are inclusive.
      if (spec[i] == c) return match;
    }
  }

  return !match;
}

bool Util::MatchSpec(std::string_view spec, std::string_view s) {
  for (char c : s) {
    if (!MatchSpec(spec, c)) {
      return false;
    }
  }

  return true;
}

std::function<bool(char)> Util::CharSpec(std::string_view spec) {
  std::bitset<256> contained;
  for (int i = 0; i < 256; i++) {
    contained[i] = MatchSpec(spec, (char)i);
  }

  return std::function<bool(char)>{
    [table = std::move(contained)](char c) {
      return !!table[(unsigned char)c];
    }};
}

bool Util::MatchesWildcard(string_view wildcard_, string_view s) {
  // Normalize to remove strings of asterisks; this is the same
  // as a single asterisk but makes matching more expensive (and
  // complicates the lookahead approach used in the * case below).
  string wildcard;
  wildcard.reserve(wildcard.size());
  bool last_star = false;
  for (size_t i = 0; i < wildcard_.size(); i++) {
    if (wildcard_[i] == '*') {
      if (!last_star)
        wildcard.push_back('*');
      last_star = true;
    } else {
      wildcard.push_back(wildcard_[i]);
      last_star = false;
    }
  }

  // We think of the wildcard as a finite state machine; this gives
  // the set of states (as indices into the wildcard) that we
  // might be at.
  std::unordered_set<size_t> pos = {0};

  for (size_t i = 0; i < s.size(); i++) {
    // Next character to match.
    const char sc = s[i];
    if (pos.empty()) return false;
    std::unordered_set<size_t> new_pos;
    for (size_t x : pos) {
      // After the last char is a valid position, but can't be
      // matched since we know there are more characters in s.
      if (x < wildcard.size()) {
        const char wc = wildcard[x];
        switch (wc) {
        case '?':
          // Matches any single byte.
          new_pos.insert(x + 1);
          break;

        case '*':
          // Here we can stay in the same position
          // OR proceed to the next.
          new_pos.insert(x);
          // XXX Seems like there should be a cleaner
          // way to do this?
          if (x + 1 < wildcard.size() &&
              (wildcard[x + 1] == '?' ||
               sc == wildcard[x + 1])) {
            new_pos.insert(x + 2);
          }
          break;

        default:
          if (sc == wc)
            new_pos.insert(x + 1);
          break;
        }
      }
    }

    pos = std::move(new_pos);
  }

  // Need to consume the entire wildcard, unless it
  // ends with a *.
  auto Contains = [&pos](size_t p) {
        return pos.find(p) != pos.end();
      };

  return Contains(wildcard.size()) ||
    (!wildcard.empty() && wildcard[wildcard.size() - 1] == '*' &&
     Contains(wildcard.size() - 1));
}

bool Util::library_matches(char k, const string &s) {
  /* skip symbolic */
  unsigned int idx = 0;
  while (idx < s.length() && (!isalnum(s[idx]))) idx++;

  /* skip 'the' */
  if (s.length() >= (idx + 5) &&
      (s[idx]|32) == 't' &&
      (s[idx + 1]|32) == 'h' &&
      (s[idx + 2]|32) == 'e' &&
      (s[idx + 3])    == ' ') return (s[idx + 4]|32) == (k|32);
  else return (s.length() > 0 && (s[idx]|32) == (k|32));
}

/* try a few methods to remove a file.
   An executable can't remove itself in
   Windows 98, though.

   XXX Remove escape-specific logic in here.
   Can just use remove from stdio.h

*/
bool Util::RemoveFile(std::string_view filename) {
  if (!ExistsFile(filename)) return true;
  else {
    std::string f{filename};
# ifdef WIN32
    /* We can do this by:
       rename tmp  delme1234.exe
       exec(delme1234.exe "-replace" "escape.exe")
          (now, the program has to have a flag -replace
           that instructs it to replace escape.exe
           with itself, then exit)
       .. hopefully exec will unlock the original
       process's executable!! */

    /* try unlinking. if that fails,
       rename it away. */
    if (0 == unlink(f.c_str())) return true;

    string fname = tempfile(".deleteme");
    if (0 == rename(f.c_str(), fname.c_str())) return true;

# else /* posix */
    if (0 == unlink(f.c_str())) return true;
# endif
  }
  return false;
}

bool Util::RelocateFile(std::string_view src, std::string_view dst) {
  namespace fs = std::filesystem;
  fs::path p1 = src;
  fs::path p2 = dst;

  std::error_code error;
  error.clear();
  fs::rename(p1, p2, error);

  return !error;
}

std::string Util::BackupFile(std::string_view src) {
  uint64_t ctr = time(nullptr);
  std::string newfile;
  do {
    ctr++;
    ctr *= 0xDECADE;
    ctr = std::rotr<uint64_t>(ctr, 11);
    newfile = std::string(src) + itos(ctr & 0x7FFFFFFF) + ".old";
    Print("Try {}\n", newfile);
  } while (Util::ExistsFile(newfile));

  if (!Util::RelocateFile(src, newfile))
    return "";
  return newfile;
}

bool Util::CopyFileBytes(std::string_view src, std::string_view dst) {
  std::string fsrc{src}, fdst{dst};
  FILE *s = fopen(fsrc.c_str(), "rb");
  if (!s) {
    return false;
  }
  FILE *d = fopen(fdst.c_str(), "wb");
  if (!d) {
    fclose(s);
    return false;
  }

  static constexpr int BUF_SIZE = 16384;
  uint8 buf[BUF_SIZE];
  int x = 0;
  do {
    /* XXX doesn't distinguish error from EOF, but... */
    x = (int)fread(buf, 1, BUF_SIZE, s);
    if (x > 0) {
      if ((signed)fwrite(buf, 1, x, d) < x) {
        fclose(s);
        fclose(d);
        return false;
      }
    }
  } while (x == BUF_SIZE);

  fclose(s);
  fclose(d);
  return true;
}

string Util::BinaryDir(string_view argv0) {
  return PathOf(argv0);
}

string Util::DirPlus(string_view dir_in, string_view file) {
  if (dir_in.empty()) return std::string(file);
  if (!file.empty() && IsDirSep(file[0])) return std::string(file);
  string dir = std::string(dir_in);
  if (!IsDirSep(dir.back()))
    dir += DIRSEPC;
  return dir + std::string(file);
}

string Util::cdup(const string &dir) {
  /* Find the last / */
  // XXX use IsDirSep here
  size_t idx = dir.rfind(DIRSEP, dir.length() - 1);
  if (idx != string::npos) {
    if (idx) return dir.substr(0, idx);
    else return ".";
  } else return ".";
}

void Util::CreatePathFor(const string &f) {
  string s;
  for (unsigned int i = 0; i < f.length();  i++) {
    if (IsDirSep(f[i])) {
      /* initial / will cause s == "" for first
         appearance */
      if (s != "") MakeDir(s);
    }
    s += f[i];
  }
}

FILE *Util::fopenp(const string &f, const string &m) {
  CreatePathFor(f);
  return fopen(f.c_str(), m.c_str());
}

string Util::Join(std::span<const string> parts,
                  std::string_view sep) {
  if (parts.empty()) return "";
  if (parts.size() == 1) return parts[0];
  size_t result_len = 0;
  for (const string &part : parts)
    result_len += part.size();
  result_len += sep.size() * (parts.size() - 1);

  string out;
  out.reserve(result_len);
  out += parts[0];
  // PERF could blit directly and avoid += capacity checks?
  for (size_t i = 1; i < parts.size(); i++) {
    out += sep;
    out += parts[i];
  }
  return out;
}

vector<string> Util::Split(std::string_view s, char sep) {
  return Fields(s, [sep](char c) { return c == sep; });
}

vector<string> Util::Tokenize(std::string_view s, char sep) {
  return Tokens(s, [sep](char c) { return c == sep; });
}

std::string_view Util::NextToken(std::string_view *str, char sep) {
  while (!str->empty() && (*str)[0] == sep) str->remove_prefix(1);

  std::string_view ret = *str;
  size_t len = 0;
  while (!str->empty() && (*str)[0] != sep) {
    len++;
    str->remove_prefix(1);
  }

  // Trim to the token.
  ret = ret.substr(0, len);

  // Remove more separator characters, which makes it easier for the
  // caller to tell if there are no more tokens.
  while (!str->empty() && (*str)[0] == sep) str->remove_prefix(1);

  return ret;
}

std::string_view Util::NextField(std::string_view *str, char sep) {
  if (str->empty()) return *str;

  size_t pos = str->find(sep);
  if (pos == std::string_view::npos) {
    std::string_view ret = *str;
    *str = "";
    return ret;
  }

  std::string_view ret = str->substr(0, pos);
  str->remove_prefix(pos + 1);
  return ret;
}


vector<string> Util::SplitWith(std::string_view str,
                               std::string_view sep) {
  assert(!sep.empty());
  size_t pos = 0;
  vector<string> ret;
  for (;;) {
    auto next = str.find(sep, pos);
    if (next == std::string_view::npos) {
      ret.emplace_back(str.substr(pos, std::string_view::npos));
      return ret;
    } else {
      ret.emplace_back(str.substr(pos, next - pos));
      pos = next + sep.size();
    }
  }
}


string Util::Replace(std::string_view src_view,
                     std::string_view findme,
                     std::string_view rep) {
  if (findme.length() < 1) return std::string(src_view);

  // PERF: Do this in one pass (copying) instead of repeatedly calling replace,
  // which does n^2 work.

  std::string src = std::string(src_view);

  auto idx = src_view.length() - 1;
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

#if 0
// localtime is not thread safe, and the _r and _s versions
// have portability issues. It's almost like they don't want
// me to use the ancient C library in 2024! And yet, the
// std::chrono version is also unavailable!
static std::tm LocalTime(int64_t unix_timestamp) {
  auto time_point = std::chrono::system_clock::from_time_t(unix_timestamp);
  auto local_zone = std::chrono::locate_zone("local");
}
#endif

static void LocalTimeTo(int64_t unix_timestamp, std::tm *tm) {
  const time_t tt = unix_timestamp;
  #ifdef WIN32
  // On windows, localtime_s has its arguments reversed
  // compared to unix.
  localtime_s(tm, &tt);
  #elif _POSIX_C_SOURCE >= 1 || _POSIX_SOURCE
  // POSIX
  localtime_r(&tt, tm);
  #elif defined(__APPLE__)
  // Also on OS X.
  localtime_r(&tt, tm);
  #else
  #error "I don't know how to get localtime on this platform?"
  #endif
}

std::string Util::FormatTime(std::string_view fmt,
                             int64_t unix_timestamp) {
  if (fmt.empty()) return "";

  // Count output buffer size.
  std::string out_buffer;
  // %H:%M:%s %Y-%m-%d would be a typical usage.
  // Here only %Y is longer than its format string.
  // So 2x is a fairly safe initial guess.
  out_buffer.resize(fmt.size() * 2);

  // We need the format string to not be empty, because
  // this function has an ambiguous return value for
  // (potentially) empty format strings.
  std::string nonempty_fmt = std::string(fmt) + "_";

  std::tm lt;
  LocalTimeTo(unix_timestamp, &lt);

  for (;;) {
    size_t written =
      strftime(out_buffer.data(), out_buffer.size() - 1,
               nonempty_fmt.c_str(), &lt);
    if (written > 0) {
      // Success; we have the number of bytes written (not including a
      // terminating null character). We also want to strip the
      // padding _ character.
      out_buffer.resize(written - 1);
      return out_buffer;
    }

    // Otherwise, try again with more space.
    out_buffer.resize(out_buffer.size() * 2);
  }
}

// Possible to use chrono for this in c++20, but this is
// invincible.
int64_t Util::UnixTime(int year, int month, int day, int h, int m, int s) {
  static constexpr int DAYS_IN_MONTH[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  auto IsLeap = [](int y) -> bool {
      return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    };

  // Days from 1970 to target year.
  // PERF could do this without a loop...
  int days = 0;
  for (int y = 1970; y < year; y++) {
    days += 365 + (IsLeap(y) ? 1 : 0);
  }

  for (int mon = 0; mon < month - 1; mon++) {
    days += DAYS_IN_MONTH[mon % 12];
    if (mon == 1 && IsLeap(year)) {
      days++;
    }
  }

  days += (day - 1);

  int64_t total_seconds = days * int64_t{86400};
  total_seconds += (h * int64_t{3600});
  total_seconds += (m * int64_t{60});
  total_seconds += s;

  return total_seconds;
}

const uint8_t *Util::MemMem(const uint8_t *haystack, size_t n,
                            const uint8_t *needle, size_t m) {
  /*
   * Copyright (C) 2008 The Android Open Source Project
   * All rights reserved.
   *
   * Redistribution and use in source and binary forms, with or without
   * modification, are permitted provided that the following conditions
   * are met:
   *  * Redistributions of source code must retain the above copyright
   *    notice, this list of conditions and the following disclaimer.
   *  * Redistributions in binary form must reproduce the above copyright
   *    notice, this list of conditions and the following disclaimer in
   *    the documentation and/or other materials provided with the
   *    distribution.
   *
   * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
   * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
   * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
   * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
   * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
   * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
   * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
   * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   * SUCH DAMAGE.
   */
  // TODO: Nice suite of algorithms here:
  //   http://www-igm.univ-mlv.fr/~lecroq/string/

  // Added by tom7. The empty string appears at the beginning of any string.
  if (m == 0) return haystack;
  if (m > n)
    return nullptr;
  if (m > 1) [[likely]] {
    const uint8_t*  y = (const uint8_t*) haystack;
    const uint8_t*  x = (const uint8_t*) needle;
    size_t j = 0;
    size_t k = 1, l = 2;
    if (x[0] == x[1]) {
      k = 2;
      l = 1;
    }
    while (j <= n-m) {
      if (x[1] != y[j+1]) {
        j += k;
      } else {
        if (!memcmp(x+2, y+j+2, m-2) && x[0] == y[j]) {
          return &y[j];
        }
        j += l;
      }
    }
  } else {
    /* degenerate case */
    return (const uint8_t*)memchr(haystack, ((const uint8_t*)needle)[0], n);
  }
  return nullptr;
}
