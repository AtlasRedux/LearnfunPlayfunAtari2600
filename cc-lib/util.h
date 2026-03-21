
#ifndef _CC_LIB_UTIL_H
#define _CC_LIB_UTIL_H

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef WIN32
#   define DIRSEP  "\\"
#   define DIRSEPC '\\'
#else
#   define DIRSEP  "/"
#   define DIRSEPC '/'
#endif

struct Util {
  using string = std::string;
  using string_view = std::string_view;

  static std::string itos(int i);
  static int stoi(const std::string &s);

  // No error handling; it just returns "".
  static string ReadFile(std::string_view filename);
  // Same but returns nullopt if the file can't be read.
  static std::optional<string> ReadFileOpt(std::string_view filename);

  // Read the remainder of stdin to a single string. Blocks until EOF
  // or error.
  static std::string ReadStdin();

  // Returns true upon success.
  static bool WriteFile(std::string_view filename,
                        std::string_view contents);

  // Reads the lines in the file to the vector. Ignores all
  // carriage returns, including ones not followed by newline.
  static std::vector<string> ReadFileToLines(std::string_view filename);
  // Overwrite file with the lines; each line ending with a newline char.
  // (Nothing special is done for newlines already in the input.)
  // Returns true upon success.
  static bool WriteLinesToFile(std::string_view filename,
                               std::span<const string> lines);

  // Frequently useful when reading a file as a vector of lines:
  // NormalizeWhitespace. Remove blank lines.
  static std::vector<string> NormalizeLines(
      std::span<const string> lines);

  // Split the string to lines. Blank lines are preserved.
  // If the file doesn't end with a newline, and the last line is
  // not empty (i.e., the file is not empty), then that line is also
  // returned. Ignores carriage returns.
  static std::vector<string> SplitToLines(string_view s);

  // Calls f on each line (without the newline), streamed from
  // the file. Ignores \r. Suitable for very large files.
  template<class F>
  static void ForEachLineInFile(std::string_view filename, F f);

  template<class F>
  static void ForEachLineInString(std::string_view contents, F f);

  // As above, but treat the first token on each line as a map key,
  // and strips leading whitespace from the rest. Ignores empty lines.
  static std::map<string, string> ReadFileToMap(std::string_view filename);

  static std::vector<uint8_t> ReadFileBytes(std::string_view filename);
  static bool WriteFileBytes(std::string_view filename,
                             std::span<const uint8_t> b);

  // Read/write a vector of uint64s in big-endian byte order.
  static std::vector<uint64_t> ReadUint64File(std::string_view filename);
  static bool WriteUint64File(std::string_view filename,
                              std::span<const uint64_t> contents);

  // Return a vector of all the files and directories in the target
  // dir. Skips ".", ".." but is not built to handle any funny business.
  static std::vector<string> ListFiles(const string &dir);

  // Join the strings in the input vector with the given delimiter.
  // Join({"a", "b", "c"}, ".") = "a.b.c"
  // Join({"z"}, ".") = "z"
  // Join({}, ".") = ""
  static string Join(std::span<const std::string> pieces,
                     std::string_view sep);

  // Split the string on the given character. Consecutive separators
  // will yield empty elements. The output always contains at least
  // one element; Split("", 'x') returns {""}.
  static std::vector<string> Split(std::string_view s, char sep);
  // The separator must be non-empty. This takes the first occurrence
  // of the separator in case of self-overlap.
  static std::vector<string> SplitWith(std::string_view str,
                                       std::string_view sep);

  // Like Split, but skipping empty fields. Result is empty if all characters
  // are separators.
  static std::vector<string> Tokenize(std::string_view str, char sep);

  // Ignore leading separators, and then get the non-separator characters
  // up until the end of the string or a separator character. The result
  // will be empty if there are no such tokens. The argument is modified
  // to skip the token and separators.
  static std::string_view NextToken(std::string_view *str, char sep);
  // If the separator does not occur, return the entire string and
  // set the view to the empty string. Otherwise, returns the string
  // up to the separator (not including it) and sets the input view
  // to the remainder of the string after that separator.
  //
  // Note that with this interface there is not really any way to
  // distinguish the number of fields in inputs like "one|two|"
  // and "one|two", but both will result in an endless stream of
  // empty fields at the end.
  static std::string_view NextField(std::string_view *str, char sep);

  // Like Split, but with an arbitrary function (char -> bool) determining
  // the separator.
  template<class F>
  static std::vector<std::string> Fields(std::string_view s, F is_sep);

  // Like Fields, but skips empty fields. Result is empty if all characters
  // are separators.
  template<class F>
  static std::vector<std::string> Tokens(std::string_view s, F is_sep);

  /* only read if the file begins with the magic string */
  static bool HasMagic(std::string_view filename, std::string_view magic);
  static string ReadFileMagic(std::string_view filename,
                              std::string_view magic);

  [[deprecated]] static unsigned int hash(const string &s);
  // give "/home/tom/" of "/home/tom/.bashrc"
  // or "/" of "/asdf" (even if asdf is a directory)
  // or "." of "file.txt"
  static string PathOf(string_view s);
  // Get ".bashrc" of "/home/tom/.bashrc"
  // or "asdf" of "/asdf" (even if asdf is a directory)
  static string FileOf(string_view s);
  // give "pdf" of "/home/tom/test.pdf"
  // For paths with no ".", returns the empty string.
  static std::string_view FileExtOf(std::string_view s);
  // give "/home/tom/test" of "/home/tom/test.pdf"
  // For paths with no ".", returns the whole string.
  static std::string_view FileBaseOf(std::string_view s);

  static string ensureext(string f, string ext);

  // Convert ASCII string to lowercase or uppercase.
  static string lcase(std::string_view in);
  static string ucase(std::string_view in);

  static bool ExistsFile(std::string_view filename);

  // Figure out the location of the binary from the argv[0] parameter.
  // This can be used to load data files that are expected to be
  // in the same directory as the binary, for example.
  static string BinaryDir(string_view argv0);

  /* DirPlus("/usr/local", "core") and
     DirPlus("/usr/local/", core")  both give  "/usr/local/core"
     DirPlus("/usr/local", "/etc/passwd")  gives "/etc/passwd"  */
  static string DirPlus(string_view dir, string_view file);

  /* spec is a character spec like "A-Z0-9`,."
     If the first character is ^, negates the spec.
     A range like A-Z is inclusive, using ASCII order. The first
     character must not come later than the second in ASCII.

     Note that some specs, like one just consisting of the character ^,
     cannot be written. */
  static bool MatchSpec(string_view spec, char c);
  static bool MatchSpec(string_view spec, string_view s);

  // Same, but returning a function object. The function tables
  // the function from char->bool and doesn't keep a reference
  // to the spec.
  static std::function<bool(char)> CharSpec(string_view spec);

  /* Returns true if s matches the wildcard; the character *
     means any sequence of bytes (or none) and the character
     ? means any single byte. */
  static bool MatchesWildcard(string_view wildcard, string_view s);

  /* An ordering on strings that gives a more "natural" sort:
     Tutorial 1, ..., Tutorial 9, Tutorial 10, Tutorial 11, ...
     rather than
     Tutorial 1, Tutorial 10, Tutorial 11, ..., Tutorial 2, Tutorial 20, ...
  */
  static int natural_compare(const string &l, const string &r);

  /* Same as above, but ignore 'the' at the beginning */
  static int library_compare(const string &l, const string &r);

  /* Is string s alphabetized under char k? */
  static bool library_matches(char k, const string &s);

  // Print a number exactly using commas to separate triples,
  // like 1,000,000.
  static string UnsignedWithCommas(uint64_t u);

  /* 0 on failure */
  static int changedir(string s);

  static int getpid();
  /* anything ending with \n. ignores \r.
     modifies str. */
  static string getline(string &str);
  /* same, for open file. */
  static string fgetline(FILE *f);

  /* chop the first token (ignoring whitespace) off
     of line, modifying line. eventually returns ""
     and line becomes empty. */
  static string chop(string &line);
  // Same, but for a string view.
  static std::string_view Chop(std::string_view *line);

  // Ignores leading and trailing whitespace. Doesn't support
  // hex prefix or plus sign.
  static int64_t ParseInt64(std::string_view s,
                            int64_t default_value = 0);
  static std::optional<int64_t> ParseInt64Opt(std::string_view s);

  static double ParseDouble(std::string_view s,
                            double default_value = 0.0);
  static std::optional<double> ParseDoubleOpt(std::string_view s);

  /* number of entries (not . or ..) in dir d */
  static int dirsize(string d);

  /* mylevels/good_tricky   to
     mylevels               to
     .
     (Currently only handles relative paths.) */
  static string cdup(const string &dir);

  // True iff big ends with small.
  static bool EndsWith(string_view big, string_view small);
  // True iff big starts with small.
  static bool StartsWith(string_view big, string_view small);

  // If the s ends with the suffix, then strip it (in the string_view
  // version, it continues to refer to the same underlying data) and
  // return true.
  static bool TryStripSuffix(string_view suffix, string_view *s);
  static bool TryStripSuffix(string_view suffix, string *s);
  // Same, for prefix.
  static bool TryStripPrefix(string_view prefix, string_view *s);
  static bool TryStripPrefix(string_view prefix, string *s);

  // Match chars where f(c) is true. Removes the longest match and
  // returns it (as a view into the same underlying data).
  template<class F>
  static std::string_view ConsumePrefixMatching(
      const F &f, std::string_view *s);

  static bool StrContains(string_view haystack, string_view needle);

  /* split the string up to the first
     occurrence of character c. The character
     is deleted from both the returned string and
     the line */
  static string ChopTo(char c, string &line);

  /* erase any whitespace up to the first
     non-whitespace char. */
  static string LoseWhiteL(const string &s);
  // Remove trailing whitespace.
  static string LoseWhiteR(string s);

  // All whitespace becomes a single space. Leading and trailing
  // whitespace is dropped.
  static string NormalizeWhitespace(std::string_view s);
  // Only removes whitesapce from beginning and end.
  static void RemoveOuterWhitespace(std::string_view *s);

  static bool IsWhitespace(char c);

  template<class F>
  static std::string RemoveCharsMatching(
      std::string_view s, const F &f);

  static std::string RemoveChar(std::string_view s, char c);

  // Pads the string to n characters by appending spaces if
  // it is shorter. (Longer strings are unmodified.) If n is
  // negative, adds space on the left instead.
  static string Pad(int n, string s);
  // Same, with the given character instead of ' '.
  static string PadEx(int n, string s, char c);

  // Parses \0, \xAA..., \r, \n, \t, \', \", \\.
  // Remember that \x takes one or more hexadecimal digit, but
  // if the byte's value is more than 0xFF, it not valid.
  // More escapes may be supported in the future.
  static std::optional<std::vector<uint8_t>>
  UnescapeC(std::string_view str);

  /* try to remove the file. If it
     doesn't exist or is successfully
     removed, then return true. */
  static bool RemoveFile(std::string_view filename);

  /* move a file from src to dst. Return
     true on success.
     (n.b. that "MoveFile" is #defined on some windows platforms)
  */
  static bool RelocateFile(std::string_view src, std::string_view dst);

  // Move a file to a new name (arbitrary) to make room for
  // "overwriting" it. Returns the new filename. Should not fail
  // unless there's an underlying filesystem problem (or src doesn't
  // exist), but it returns "" in that case.
  static std::string BackupFile(std::string_view src);

  /* make a copy by reading/writing */
  static bool CopyFileBytes(std::string_view src, std::string_view dst);

  static string tempfile(const string &suffix);

  /* does this file exist and is it a directory? */
  static bool isdir(std::string_view s);

  /* same as isdir */
  static bool existsdir(const string &d);

  static bool MakeDir(const string &s);

  /* creates directories for f */
  static void CreatePathFor(const string &f);

  /* open, creating directories if necessary */
  static FILE *fopenp(const string &f, const string &mode);

  /* replace all occurrences of 'findme' with 'replacewith' in 'src' */
  static string Replace(std::string_view src, std::string_view findme,
                        std::string_view replacewith);

  /* called minimum, maximum because some includes
     define these with macros, ugh */
  static int minimum(int a, int b) {
    if (a < b) return a;
    else return b;
  }

  static int maximum(int a, int b) {
    if (a > b) return a;
    else return b;
  }

  // Returns true if c is a hex digit (0-9a-fA-F). "Digit" is of course a
  // misnomer.
  static bool IsHexDigit(char c);
  // Returns 0-15 for valid hex digits (0-9a-fA-F) and arbitrary (really,
  // it's weird) values for other chars.
  static int HexDigitValue(char c);

  // For a number in 0-15, return its corresponding hex digit (lowercase).
  // (Otherwise, returns something arbitrary.)
  static char HexDigit(int v);

  // Convert each byte of the input into two lowercase hex digits.
  // If non-null, each byte is prefixed by the prefix, and each pair of
  // bytes is separated by the separator.
  static std::string HexString(std::string_view s,
                               const char *sep = nullptr,
                               const char *prefix = nullptr);

  // Requires that the string consist only of '1's and '0's, and
  // the bit width cannot exceed 64 (even with leading zeroes).
  // May not be empty.
  static std::optional<uint64_t> ParseBinary(std::string_view s);
  // Accepting 0-9A-Fa-f. No 0x prefix allowed. Can't exceed 16
  // characters (even with leading zeroes). May not be empty.
  static std::optional<uint64_t> ParseHex(std::string_view s);

  // See utf8.h for UTF utilities that used to be here.

  // See factorization.h for factoring that used to be here.

  // Uses format strings from strftime.
  // Good: %H:%M:%S (24h)
  // ISO 8601 dates: %Y-%m-%d
  // %d %b %Y  (27 Sep 1979)
  static std::string FormatTime(std::string_view fmt,
                                int64_t unix_timestamp);

  // Return a unix timestamp for the moment in time, which
  // must be after 1970. The time is interpreted as UTC.
  // Month and day are 1-based, like humans use.
  // h is 24-hour time.
  // No validation is performed; the day might not be in the
  // month, for example.
  static int64_t UnixTime(
      int year, int month, int day, int h, int m, int s);

  // this is memmem, which is in glibc but not std C or C++.
  static const uint8_t *MemMem(const uint8_t *haystack, size_t n,
                               const uint8_t *needle, size_t m);
};

// Template implementations follow.

template<class F>
void Util::ForEachLineInFile(std::string_view filename, F f) {
  std::string s{filename};
  FILE *file = fopen(s.c_str(), "rb");
  if (!file) return;
  int c;
  std::string line;
  while ( (c = fgetc(file), c != EOF) ) {
    if (c == '\r') continue;
    if (c == '\n') {
      f(std::move(line));
      line.clear();
    } else {
      line += c;
    }
  }
  // Don't require trailing newline.
  if (!line.empty()) f(line);
  fclose(file);
}


template<class F>
void Util::ForEachLineInString(std::string_view sv, F f) {
  while (!sv.empty()) {
    size_t pos = sv.find('\n');
    // If newline isn't found, this is the remainder.
    std::string_view line = sv.substr(0, pos);
    if (pos == std::string_view::npos) {
      // This is the last line.
      sv = {};
    } else {
      sv.remove_prefix(pos + 1);
    }

    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    f(line);
  }
}

template<class F>
std::vector<std::string> Util::Tokens(std::string_view s, F f) {
  std::vector<std::string> out;
  int64_t start = 0;
  for (int64_t i = 0; i < (int64_t)s.size(); i++) {
    if (f(s[i])) {
      if (i - start != 0) {
        // Substring constructor.
        out.emplace_back(s, start, i - start);
      }
      start = i + 1;
      // (i incremented in loop)
    }
  }
  if (start != (int64_t)s.size())
    out.emplace_back(s, start, string::npos);
  return out;
}

template<class F>
std::vector<std::string> Util::Fields(std::string_view s, F f) {
  std::vector<std::string> out;
  int64_t start = 0;
  for (int64_t i = 0; i < (int64_t)s.size(); i++) {
    if (f(s[i])) {
      // Substring constructor.
      out.emplace_back(s, start, i - start);
      start = i + 1;
      // (i incremented in loop)
    }
  }
  out.emplace_back(s, start, string::npos);
  return out;
}

template<class F>
std::string_view Util::ConsumePrefixMatching(
    const F &f, std::string_view *s) {
  std::string_view orig = *s;
  size_t prefix_len = 0;
  for (;;) {
    if (s->empty()) {
      return orig;
    }

    const char c = (*s)[0];
    if (f(c)) {
      s->remove_prefix(1);
      prefix_len++;
    } else {
      return orig.substr(0, prefix_len);
    }
  }
  // whole string consumed.
  return orig;
}

template<class F>
std::string Util::RemoveCharsMatching(
    std::string_view s, const F &f) {
  std::string ret;
  ret.reserve(s.size());
  for (int i = 0; i < (int)s.size(); i++) {
    char c = s[i];
    if (!f(c)) ret.push_back(c);
  }
  return ret;
}


#endif
