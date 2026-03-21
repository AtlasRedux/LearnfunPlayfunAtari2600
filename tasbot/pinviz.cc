/* This program creates a graphic summarizing
   Atari 2600 play by averaging screen frames.

   Based on the NES pinball visualizer (scopefun.cc derivative).
   The NES-specific room detection has been removed — this version
   simply averages all frames into a single composite image.
   Game-specific room/state detection can be added per-game.
 */

#include <vector>
#include <string>
#include <set>

#include "platform.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "tasbot.h"

#include "types.h"
#include "basis-util.h"
#include "emulator.h"
#include "simpleinput.h"
#include "motifs.h"
#include "../cc-lib/arcfour.h"
#include "util.h"
#include "../cc-lib/textsvg.h"
#include "pngsave.h"
#include "wave.h"
#include "../cc-lib/stb_image.h"

#if MARIONET
#include "marionet.pb.h"
#include "netutil.h"
using ::google::protobuf::Message;
#endif

// Atari 2600 screen dimensions.
static const int SCREEN_WIDTH = 160;
static const int SCREEN_HEIGHT = 210;

struct PinViz {
  PinViz(const string &game,
	   const string &moviename,
	   int sf, int mf) :
    game(game) {

    map<string, string> config = Util::ReadFileToMap("config.txt");
    string romfile = config.count("rom") ? config["rom"] : (game + ".a26");
    Emulator::Initialize(romfile);

    movie = SimpleInput::ReadInputs(moviename);

    if (sf < 0) sf = 0;
    if (!mf) mf = movie.size();
    startframe = sf;
    maxframe = mf;

    rgba4x = (uint8 *) malloc(sizeof (uint8) * (width * 4) * (height * 4) * 4);
    CHECK(rgba4x);

    ClearBuffer();
  }

  void ClearBuffer() {
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
	rgba[y * width * 4 + x * 4 + 0] = 0;
	rgba[y * width * 4 + x * 4 + 1] = 0;
	rgba[y * width * 4 + x * 4 + 2] = 0;
	rgba[y * width * 4 + x * 4 + 3] = 0xFF;
      }
    }
  }

  inline void WritePixel(int x, int y,
			 uint8 r, uint8 g, uint8 b, uint8 a) {
    CHECK(x >= 0);
    CHECK(y >= 0);
    CHECK(x < width);
    CHECK(y < height);

    rgba[y * width * 4 + x * 4 + 0] = r;
    rgba[y * width * 4 + x * 4 + 1] = g;
    rgba[y * width * 4 + x * 4 + 2] = b;
    rgba[y * width * 4 + x * 4 + 3] = 0xFF;
  }

  void CopyTo4x() {
    const int PXSIZE = 4;
    const int BPP = 4;
    const int width4x = width * PXSIZE;
#   define SMALLPX(x, y) ((y) * width * BPP + (x) * BPP)
#   define BIGPX(x, y) (((y) * PXSIZE) * width4x * BPP + ((x) * PXSIZE) * BPP)
#   define PXOFFSET(xo, yo) (((xo) * BPP) + ((yo) * width4x * BPP))
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
	uint8 r = rgba[SMALLPX(x, y) + 0];
	uint8 g = rgba[SMALLPX(x, y) + 1];
	uint8 b = rgba[SMALLPX(x, y) + 2];
	uint8 a = rgba[SMALLPX(x, y) + 3];

	for (int yo = 0; yo < PXSIZE; yo++) {
	  for (int xo = 0; xo < PXSIZE; xo++) {
	    rgba4x[BIGPX(x, y) + PXOFFSET(xo, yo) + 0] = r;
	    rgba4x[BIGPX(x, y) + PXOFFSET(xo, yo) + 1] = g;
	    rgba4x[BIGPX(x, y) + PXOFFSET(xo, yo) + 2] = b;
	    rgba4x[BIGPX(x, y) + PXOFFSET(xo, yo) + 3] = a;
	  }
	}

      }
    }
#   undef PXOFFSET
#   undef BIGPX
#   undef SMALLPX
  }

  void Save4x(const string &filename) {
    static const int PXSIZE = 4;
    const int width4x = width * PXSIZE;
    const int height4x = height * PXSIZE;
    CHECK(PngSave::SaveAlpha(filename, width4x, height4x, rgba4x));
  }

  void SaveAV(const string &dir) {
    vector< vector<uint8> > screens;

    for (int i = startframe;
	 i < (int)movie.size() && i < maxframe + 2; i++) {
      vector<uint8> screen;

      if (i % 100 == 0) fprintf(stderr, "%d.\n", i);
      Emulator::StepFull(movie[i]);
      Emulator::GetImage(&screen);
      screens.push_back(screen);
    }

    // Average all frames into a single composite.
    vector<double> mixed;
    mixed.resize(width * height * 3, 0.0);
    int num_frames = 0;

    for (int i = 0; i < (int)screens.size(); i++) {
      if (i % 100 == 0) {
	fprintf(stderr, "%d of %d...\n", i, (int)screens.size());
      }
      num_frames++;
      Mix(screens[i], &mixed);
    }

    fprintf(stderr, "%d frames averaged.\n", num_frames);
    MapAndWrite(StringPrintf("%s/%s-average.png", dir.c_str(), game.c_str()),
                mixed, num_frames);

    fprintf(stderr, "Done.\n");
  }

  uint8 MapFrac(double r, int num, double darkest, double lightest) {
    const double norm = r / num;
    const double gap = lightest - darkest;

    double val;
    if (norm < 0.20) {
      val = 0.15 + 0.10 * ((norm - darkest) / gap);
    } else {
      val = 0.25 + (0.75 * norm);
    }

    int b = val * 255;
    if (b > 255) return 255;
    if (b < 0) return 0;
    return (uint8) b;
  }

  struct RGBL {
    RGBL(double r, double g, double b) : r(r), g(g), b(b) {
      lum = max(r, max(g, b));
    }
    double r;
    double g;
    double b;
    double lum;
  };

  static bool CompareRGBL(const RGBL &a, const RGBL &b) {
    return a.lum < b.lum;
  }

  static bool RGBLEq(const RGBL &a, const RGBL &b) {
    return a.lum == b.lum;
  }

  static int GetIndex(const vector<RGBL> &vec, RGBL rgbl) {
    return std::distance(vec.begin(),
			 std::lower_bound(vec.begin(), vec.end(), rgbl,
					  CompareRGBL));
  }

  static double GetFrac(const vector<RGBL> &vec, RGBL rgbl) {
    return GetIndex(vec, rgbl) / (double)vec.size();
  }

  void MapAndWrite(const string &filename,
		   const vector<double> &mixed,
		   int num) {
    ClearBuffer();
    CHECK(width * height * 3 == (int)mixed.size());

    double darkest = 1.0, lightest = 0.0;
    vector<RGBL> all_colors;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
	double r = mixed[(y * width + x) * 3 + 0];
	double g = mixed[(y * width + x) * 3 + 1];
	double b = mixed[(y * width + x) * 3 + 2];

	all_colors.push_back(RGBL(r, g, b));

	bool black = (r < (1 / 256.0)) &&
	  (g < (1 / 256.0)) &&
	  (b < (1 / 256.0));

	double lum = max(r, max(g, b));

	if (!black) {
	  darkest = min(darkest, lum);
	  lightest = max(lightest, lum);
	}
      }
    }

    std::sort(all_colors.begin(), all_colors.end(), &CompareRGBL);
    all_colors.erase(std::unique(all_colors.begin(), all_colors.end(), &RGBLEq),
                     all_colors.end());

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
	double r = mixed[(y * width + x) * 3 + 0];
	double g = mixed[(y * width + x) * 3 + 1];
	double b = mixed[(y * width + x) * 3 + 2];

	bool black = (r < (1 / 256.0)) &&
	  (g < (1 / 256.0)) &&
	  (b < (1 / 256.0));

	uint8 rr, gg, bb;
	if (black) {
	  rr = gg = bb = 0;
	} else {
	  double ord = GetFrac(all_colors, RGBL(r, g, b));
	  double len = sqrt(r * r + g * g + b * b);
	  double norm_r = r / len;
	  double norm_g = g / len;
	  double norm_b = b / len;
	  double scale_r = norm_r * ord;
	  double scale_g = norm_g * ord;
	  double scale_b = norm_b * ord;

	  rr = scale_r * 255.0;
	  gg = scale_g * 255.0;
	  bb = scale_b * 255.0;
	}
	WritePixel(x, y, rr, gg, bb, 255);
      }
    }

    CopyTo4x();
    Save4x(filename);
  }

  void Mix(const vector<uint8> &screen,
	   vector<double> *mixed) {
    // Screen may be SCREEN_WIDTH x SCREEN_HEIGHT x 4 (RGBA).
    // Our canvas is width x height; only mix what fits.
    for (int y = 0; y < min(SCREEN_HEIGHT, height); y++) {
      for (int x = 0; x < min(SCREEN_WIDTH, width); x++) {
        int si = 4 * (y * SCREEN_WIDTH + x);
        if (si + 2 >= (int)screen.size()) break;
        double r = screen[si + 0];
        double g = screen[si + 1];
        double b = screen[si + 2];
        int di = (y * width + x) * 3;
        (*mixed)[di + 0] += r / 255.0;
        (*mixed)[di + 1] += g / 255.0;
        (*mixed)[di + 2] += b / 255.0;
      }
    }
  }

  int startframe, maxframe;

  // Atari 2600 resolution.
  static const int width = SCREEN_WIDTH;
  static const int height = SCREEN_HEIGHT;
  uint8 rgba[width * height * 4];
  uint8 *rgba4x;

  string game;
  vector<uint8> movie;
};

int main(int argc, char *argv[]) {
  #if MARIONET
  fprintf(stderr, "Init networking\n");
  {
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    CHECK(wsaResult == 0);
  }
  fprintf(stderr, "Networking initialized OK.\n");
  #endif

  map<string, string> config = Util::ReadFileToMap("config.txt");
  const string game = config["game"];
  string moviename;
  if (argc > 2) {
    fprintf(stderr, "This program takes at most one argument.\n");
    exit(1);
  } else if (argc == 2) {
    moviename = argv[1];
  } else {
    moviename = game + "-playfun-futures-progress.a26inp";
    fprintf(stderr, "With no command line argument, assuming movie name\n"
	    "%s\n"
	    " .. from config file.\n", moviename.c_str());
  }
  CHECK(!game.empty());
  CHECK(!moviename.empty());

  int startframe = atoi(config["startframe"].c_str());
  int maxframe = atoi(config["maxframe"].c_str());

  PinViz pv(game, moviename, startframe, maxframe);
  string dir = game + "-movie";
  Util::MakeDir(dir);
  pv.SaveAV(dir);

  Emulator::Shutdown();

  #if MARIONET
  WSACleanup();
  #endif
  return 0;
}
