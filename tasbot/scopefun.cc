/* Visualization tool — renders per-frame objective analysis as
   PNG image sequence + WAV audio.

   Ported from NES version. Resolution and input constants adapted
   for Atari 2600; controller overlay removed (no graphic assets).
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
#include "weighted-objectives.h"
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

struct Graphic {
  // From a PNG file.
  explicit Graphic(const string &filename) {
    int bpp;
    uint8 *stb_rgba = stbi_load(filename.c_str(),
				&width, &height, &bpp, 4);
    CHECK(stb_rgba);
    for (int i = 0; i < width * height * bpp; i++) {
      rgba.push_back(stb_rgba[i]);
    }
    stbi_image_free(stb_rgba);
    fprintf(stderr, "%s is %dx%d @%dbpp.\n",
	    filename.c_str(), width, height, bpp);
  }

  int width, height;
  vector<uint8> rgba;
};

struct ScopeFun {
  ScopeFun(const string &game,
	   const string &moviename,
	   int sf, int mf, bool so, bool fo) :
    game(game),
    soundonly(so),
    fatobjectivelines(fo) {

    map<string, string> config = Util::ReadFileToMap("config.txt");
    string romfile = config.count("rom") ? config["rom"] : (game + ".a26");
    Emulator::Initialize(romfile);
    objectives = WeightedObjectives::LoadFromFile(game + ".objectives");
    CHECK(objectives);
    fprintf(stderr, "Loaded %d objective functions\n", (int)objectives->Size());

    movie = SimpleInput::ReadInputs(moviename);

    if (sf < 0) sf = 0;
    if (!mf) mf = movie.size();
    startframe = sf;
    maxframe = mf;

    rgba4x = (uint8 *) malloc(sizeof (uint8) * (width * 4) * (height * 4) * 4);
    CHECK(rgba4x);

    ClearBuffer();

    if (soundonly) {
      fprintf(stderr, "sound-only mode.\n");
    }
  }

  // Make opaque black.
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

  struct MemFact {
    MemFact() : ups(0), downs(0) {}
    double ups, downs;
    double allups, alldowns;
    double involved;
  };

  enum Order {
    DOWN, SAME, UP,
  };

  struct ObjFact {
    ObjFact() : obj(NULL), weight(0.0), order(SAME) {}
    const vector<int> *obj;
    double weight;
    Order order;
  };

  void MakeMemFacts(const vector<uint8> &mem1,
		    const vector<uint8> &mem2,
		    vector<MemFact> *facts,
		    vector<ObjFact> *objfacts,
		    double *total_weight) {
    *total_weight = 0;

    vector< pair<const vector<int> *, double> > objs = objectives->GetAll();
    objfacts->clear();
    objfacts->resize(objs.size());

    facts->clear();
    // Atari 2600: 128 bytes of RAM.
    facts->resize(128);

    for (int o = 0; o < (int)objs.size(); o++) {
      const vector<int> &obj = *objs[o].first;
      double weight = objs[o].second;
      *total_weight += weight;
      (*objfacts)[o].obj = &obj;
      (*objfacts)[o].weight = weight;

      Order order = SAME;
      for (int i = 0; i < (int)obj.size(); i++) {
	int p = obj[i];
	if (mem1[p] > mem2[p]) {
	  (*facts)[p].downs += weight;
	  order = DOWN;
	  break;
	}
	if (mem1[p] < mem2[p]) {
	  (*facts)[p].ups += weight;
	  order = UP;
	  break;
	}
      }

      (*objfacts)[o].order = order;

      for (int i = 0; i < (int)obj.size(); i++) {
	int p = obj[i];
	switch (order) {
	case UP: (*facts)[p].allups += weight; break;
	case DOWN: (*facts)[p].alldowns += weight; break;
	default:;
	}
	(*facts)[p].involved += weight;
      }

    }
  }

  inline void WritePixel(int x, int y,
			 uint8 r, uint8 g, uint8 b, uint8 a) {
    DCHECK(x >= 0);
    DCHECK(y >= 0);
    DCHECK(x < width);
    DCHECK(y < height);

    rgba[y * width * 4 + x * 4 + 0] = r;
    rgba[y * width * 4 + x * 4 + 1] = g;
    rgba[y * width * 4 + x * 4 + 2] = b;
    rgba[y * width * 4 + x * 4 + 3] = 0xFF;
  }

  inline uint32 GetPixel(int x, int y) {
    DCHECK(x >= 0);
    DCHECK(y >= 0);
    DCHECK(x < width);
    DCHECK(y < height);

    uint32 r = rgba[y * width * 4 + x * 4 + 0];
    uint32 g = rgba[y * width * 4 + x * 4 + 1];
    uint32 b = rgba[y * width * 4 + x * 4 + 2];
    uint32 a = rgba[y * width * 4 + x * 4 + 3];
    return (r << 24) | (g << 16) | (b << 8) | a;
  }

  static inline void WritePixelTo(int x, int y,
				  uint8 r, uint8 g, uint8 b,
				  uint8 *vec,
				  int ww, int hh) {
    DCHECK(x >= 0);
    DCHECK(y >= 0);
    DCHECK(x < ww);
    DCHECK(y < hh);

    vec[y * ww * 4 + x * 4 + 0] = r;
    vec[y * ww * 4 + x * 4 + 1] = g;
    vec[y * ww * 4 + x * 4 + 2] = b;
    vec[y * ww * 4 + x * 4 + 3] = 0xFF;
  }

  static inline void WritePixelAlphaTo(int x, int y,
				       uint8 r, uint8 g, uint8 b, uint8 a,
				       uint8 *vec,
				       int ww, int hh) {
    DCHECK(x >= 0);
    DCHECK(y >= 0);
    DCHECK(x < ww);
    DCHECK(y < hh);

    uint32 oldr = vec[y * ww * 4 + x * 4 + 0];
    uint32 oldg = vec[y * ww * 4 + x * 4 + 1];
    uint32 oldb = vec[y * ww * 4 + x * 4 + 2];
    uint32 oma = 0xFF - a;

    uint32 mixr = ((uint32)r * a + oldr * oma) >> 8;
    uint32 mixg = ((uint32)g * a + oldg * oma) >> 8;
    uint32 mixb = ((uint32)b * a + oldb * oma) >> 8;
    CHECK(mixr >= 0);
    CHECK(mixg >= 0);
    CHECK(mixb >= 0);
    CHECK(mixr <= 255);
    CHECK(mixg <= 255);
    CHECK(mixb <= 255);

    vec[y * ww * 4 + x * 4 + 0] = mixr;
    vec[y * ww * 4 + x * 4 + 1] = mixg;
    vec[y * ww * 4 + x * 4 + 2] = mixb;
    vec[y * ww * 4 + x * 4 + 3] = 0xFF;
  }

  void BlitGraphicRect(const Graphic &graphic,
		       int rectx, int recty,
		       int rectw, int recth,
		       int dstx, int dsty) {
    BlitAlpha(graphic.width, graphic.height, rectx, recty,
	      rectw, recth, dstx, dsty,
	      graphic.rgba);
  }

  void BlitGraphic(const Graphic &graphic,
		   int dstx, int dsty) {
    BlitAlpha(graphic.width, graphic.height, 0, 0,
	      graphic.width, graphic.height, dstx, dsty,
	      graphic.rgba);
  }

  void Blit(int srcwidth, int srcheight,
	    int rectx, int recty,
	    int rectw, int recth,
	    int dstx, int dsty,
	    const vector<uint8> &srcrgba) {
    for (int y = 0; y < recth; y++) {
      for (int x = 0; x < rectw; x++) {
	uint8 r, g, b, a;

	r = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 0];
	g = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 1];
	b = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 2];
	a = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 3];

	WritePixel(dstx + x, dsty + y, r, g, b, a);
      }
    }
  }

  void BlitAlpha(int srcwidth, int srcheight,
		 int rectx, int recty,
		 int rectw, int recth,
		 int dstx, int dsty,
		 const vector<uint8> &srcrgba) {
    for (int y = 0; y < recth; y++) {
      for (int x = 0; x < rectw; x++) {
	uint32 r, g, b, a;

	r = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 0];
	g = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 1];
	b = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 2];
	a = srcrgba[(y + recty) * srcwidth * 4 + (x + rectx) * 4 + 3];

	if (a != 0xFF) {
	  uint32 old = GetPixel(dstx + x, dsty + y);
	  uint32 oldr = 0xFF & (old >> 24);
	  uint32 oldg = 0xFF & (old >> 16);
	  uint32 oldb = 0xFF & (old >>  8);
	  uint32 oma = 0xFF - a;

	  uint32 mixr = (r * a + oldr * oma) >> 8;
	  uint32 mixg = (g * a + oldg * oma) >> 8;
	  uint32 mixb = (b * a + oldb * oma) >> 8;

	  WritePixel(dstx + x, dsty + y, mixr, mixg, mixb, 0xFF);
	} else {
	  WritePixel(dstx + x, dsty + y, r, g, b, 0xFF);
	}
      }
    }
  }

  struct Score {
    static const int SCOREH = 32;
    Score(int green, int red, int blue)
      : green(green), red(red), blue(blue) {}
    int green;
    int red;
    int blue;
  };

  Score GetScore(const vector<ObjFact> &objfacts) {
    double total = 0.0;
    double gtotal = 0.0, rtotal = 0.0;
    for (int i = 0; i < (int)objfacts.size(); i++) {
      total += objfacts[i].weight;
      switch (objfacts[i].order) {
      case UP: gtotal += objfacts[i].weight; break;
      case DOWN: rtotal += objfacts[i].weight; break;
      default:;
      }
    }

    int green = floor((gtotal / total) * Score::SCOREH);
    int red = floor((rtotal / total) * Score::SCOREH);
    int blue = Score::SCOREH - red - green;
    return Score(green, red, blue);
  }

  void WriteScoreTo(int xstart, int ystart,
		    Score score, int swidth) {
    for (int y = 0; y < Score::SCOREH; y++) {
      uint8 r, g, b;
      if (y < score.blue) {
	r = g = 0;
	b = 0x7F;
      } else if (y < score.blue + score.red) {
	b = g = 0;
	r = 0xCC;
      } else {
	r = b = 0;
	g = 0xCC;
      }
      for (int x = 0; x < swidth; x++) {
	WritePixel(x + xstart, y + ystart, r, g, b, 0xFF);
      }
    }
  }

  void WriteRAMTo(int xstart, int ystart,
		  const vector<uint8> &mem,
		  const vector<MemFact> &memfacts,
		  double total_weight) {
    // Atari 2600: 128 bytes, laid out as 16x8.
    static const int MEMW = 16;
    static const int MEMH = 8;
    CHECK(MEMW * MEMH == (int)mem.size());

    for (int y = 0; y < MEMH; y++) {
      for (int x = 0; x < MEMW; x++) {
	int idx = y * MEMW + x;
	uint8 byte = mem[idx];

	double value = 0.3 + 0.7 * (byte / 255.0);

	static const double RED_HUE = 0.0;
	static const double GREEN_HUE = 120.0;
	static const double BLUE_HUE = 240.0;

	double hue = 0.0, saturation = 0.0;
	if (memfacts[idx].allups == 0.0 &&
	    memfacts[idx].alldowns == 0.0) {
	  if (memfacts[idx].involved > 0) {
	    hue = BLUE_HUE;
	    saturation = 0.5 + 0.5 * (memfacts[idx].involved / total_weight);
	  } else {
	    hue = 0.0;
	    saturation = 0.0;
	  }
	} else {
	  if (memfacts[idx].ups > 0.0 &&
	      memfacts[idx].ups > memfacts[idx].downs) {
	    hue = GREEN_HUE;
	    saturation = 1.0;
	  } else if (memfacts[idx].downs > 0.0) {
	    hue = RED_HUE;
	    saturation = 1.0;
	  } else {
	    double sum = memfacts[idx].allups + memfacts[idx].alldowns;
	    hue = (memfacts[idx].allups / sum) * GREEN_HUE +
	      (memfacts[idx].alldowns / sum) * RED_HUE;
	    saturation = 0.5 + 0.5 * (sum / total_weight);
	  }
	}

	uint8 r, g, b;
	HSV(hue, saturation, value, &r, &g, &b);

	WritePixel(x + xstart, y + ystart, r, g, b, 0xFF);
      }
    }
  }

  void WriteNormalizedTo(int x, int y,
			 const vector< vector<uint8> > &memories,
			 const vector<uint32> &colors,
			 int now,
			 int nwidth, int nheight,
			 int surfw, int surfh,
			 uint8 *surf) {
    for (int col = 0; col <= nwidth; col++) {
      int idx = now - col;
      if (idx < 0) break;
      const vector<uint8> &mem = memories[idx];
      vector<double> vfs = objectives->GetNormalizedValues(mem);
      CHECK((int)vfs.size() == (int)colors.size());
      for (int i = 0; i < (int)vfs.size(); i++) {
	CHECK(0.0 <= vfs[i]);
	CHECK(vfs[i] <= 1.0);

	const uint32 color = colors[i];
	uint8 r = 255 & (color >> 24);
	uint8 g = 255 & (color >> 16);
	uint8 b = 255 & (color >> 8);

	if (fatobjectivelines) {
	  double omv = 1.0 - vfs[i];
	  double point = nheight * omv;
	  static const int NP = 3;
	  int start = floor(point);
	  double leftover = point - start;
	  CHECK(leftover >= 0.0);
	  CHECK(leftover <= 1.0);
	  uint8 endalpha = leftover * 255;
	  uint8 startalpha = 255 - endalpha;
	  CHECK(((uint32)endalpha + (uint32)startalpha) == 255);

	  WritePixelAlphaTo(x + col, y + start,
			    r, g, b, startalpha,
			    surf, surfw, surfh);
	  for (int p = 1; p <= NP; p++) {
	    if (start + p >= nheight) break;
	    WritePixelTo(x + col, y + start + p,
			 r, g, b,
			 surf, surfw, surfh);
	  }
	  if (start + NP + 1 < nheight) {
	    WritePixelAlphaTo(x + col, y + start + NP + 1,
			      r, g, b, endalpha,
			      surf, surfw, surfh);
	  }

	} else {
	  double yoff = nheight * (1.0 - vfs[i]);
	  WritePixelTo(x + col, y + floor(yoff),
		       r, g, b, surf, surfw, surfh);
	}
      }
    }
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
    vector< vector<uint8> > memories;
    vector< vector<uint8> > screens;

    const int STARTFRAMES = startframe;
    const int MAXFRAMES = maxframe;

    const string wavename = StringPrintf("%s/%s-%d-%d.wav",
					 dir.c_str(),
					 game.c_str(),
					 STARTFRAMES, MAXFRAMES);
    WaveFile wavefile(wavename);

    vector<uint32> colors;
    ArcFour rc("scopefun");
    for (int i = 0; i < objectives->Size(); i++) {
      colors.push_back(RandomBrightColor(&rc));
    }

    bool started = false;

    for (int i = 0; i < (int)movie.size() && i < MAXFRAMES + 2; i++) {
      vector<uint8> mem, screen;
      Emulator::GetMemory(&mem);
      memories.push_back(mem);

      if (started) objectives->Observe(mem);

      if (i % 100 == 0) fprintf(stderr, "%d.\n", i);
      Emulator::StepFull(movie[i]);
      if (movie[i]) started = true;

      Emulator::GetImage(&screen);
      // Atari 2600: 160x210x4 = 134400 bytes.
      // TODO: verify actual GetImage size for Atari.
      screens.push_back(screen);

      vector<int16> sound;
      Emulator::GetSound(&sound);
      wavefile.Write(sound);
    }
    {
      vector<uint8> mem_last;
      Emulator::GetMemory(&mem_last);
      memories.push_back(mem_last);
      if (started) objectives->Observe(mem_last);
    }

    wavefile.Close();
    fprintf(stderr, "Wrote sound.\n");

    if (soundonly) return;

    uint64 starttime = time(NULL);

    // Atari screen dimensions.
    static const int ATARI_WIDTH = 160;
    static const int ATARI_HEIGHT = 210;

    vector<Score> comp_one, comp_ten, comp_hundred;
    for (int i = STARTFRAMES; i < (int)movie.size() && i < MAXFRAMES; i++) {
      ClearBuffer();

      // Blit the Atari screen into the left portion.
      if (i < (int)screens.size() && !screens[i].empty()) {
        int sw = min(ATARI_WIDTH, width);
        int sh = min(ATARI_HEIGHT, height);
        Blit(ATARI_WIDTH, ATARI_HEIGHT, 0, 0, sw, sh, 0, 0, screens[i]);
      }

      // Previous frame objective analysis.
      {
	vector<MemFact> facts;
	vector<ObjFact> objfacts;
	double total_weight = 0;
	MakeMemFacts(memories[i], memories[i + 1],
		     &facts, &objfacts, &total_weight);
	WriteRAMTo(ATARI_WIDTH + 1, 0, memories[i + 1], facts, total_weight);

	WriteScoreAndHistoryTo(ATARI_WIDTH + 1 + 17, 0, objfacts, &comp_one,
			       width - (ATARI_WIDTH + 1 + 17));
      }

      // Ten frames.
      if (i > 10) {
	vector<MemFact> facts;
	vector<ObjFact> objfacts;

	double total_weight = 0;
	MakeMemFacts(memories[i - 9], memories[i + 1],
		     &facts, &objfacts, &total_weight);
	WriteRAMTo(ATARI_WIDTH + 1, 9, memories[i + 1], facts, total_weight);
	WriteScoreAndHistoryTo(ATARI_WIDTH + 1 + 17, 9, objfacts, &comp_ten,
			       width - (ATARI_WIDTH + 1 + 17));
      }

      // One hundred frames.
      if (i > 100) {
	vector<MemFact> facts;
	vector<ObjFact> objfacts;

	double total_weight = 0;
	MakeMemFacts(memories[i - 99], memories[i + 1],
		     &facts, &objfacts, &total_weight);
	WriteRAMTo(ATARI_WIDTH + 1, 18, memories[i + 1], facts, total_weight);
	Score score = GetScore(objfacts);
	WriteScoreAndHistoryTo(ATARI_WIDTH + 1 + 17, 18, objfacts, &comp_hundred,
			       width - (ATARI_WIDTH + 1 + 17));
      }

      CopyTo4x();

      WriteNormalizedTo(ATARI_WIDTH * 4 + 4, 27 * 4, memories, colors, i,
			(width - ATARI_WIDTH - 1) * 4, (height - 27) * 4,
			width * 4, height * 4, rgba4x);


      const string filename = StringPrintf("%s/%s-%d.png",
					   dir.c_str(), game.c_str(), i);

      Save4x(filename);
      int totalframes = min((int)movie.size(), MAXFRAMES) - STARTFRAMES;
      double seconds_elapsed = time(NULL) - starttime;
      double fps = (i - STARTFRAMES) / seconds_elapsed;
      double seconds_left = (totalframes - i) / fps;
      fprintf(stderr, "Wrote %s (%.1f%% / %.1fm left).\n",
	      filename.c_str(),
	      (100.0 * (i - STARTFRAMES)) / totalframes,
	      seconds_left / 60.0);
    }

    fprintf(stderr, "Done.\n");
  }

  void WriteScoreAndHistoryTo(int x, int y, const vector<ObjFact> &objfacts,
			      vector<Score> *history,
			      int swidth) {
    Score score = GetScore(objfacts);
    WriteScoreTo(x, y, score, 6);
    swidth -= 7;
    for (int i = 0; i < swidth; i++) {
      if (i >= (int)history->size()) break;

      const int fromback = (history->size() - 1) - i;
      WriteScoreTo(x + 7 + i, y, history->at(fromback), 1);
    }
    history->push_back(score);
  }

  int startframe, maxframe;

  // Canvas: wide enough for Atari screen (160) + RAM viz + history.
  static const int width = 480;
  static const int height = 270;
  uint8 rgba[width * height * 4];
  uint8 *rgba4x;

  WeightedObjectives *objectives;
  string game;
  vector<uint8> movie;
  const bool soundonly;
  const bool fatobjectivelines;
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
  bool soundonly = !config["soundonly"].empty();
  bool fatobjectivelines = !config["fatobjectivelines"].empty();

  ScopeFun pf(game, moviename, startframe, maxframe, soundonly,
	      fatobjectivelines);
  string dir = game + "-movie";
  Util::MakeDir(dir);
  pf.SaveAV(dir);

  Emulator::Shutdown();

  #if MARIONET
  WSACleanup();
  #endif
  return 0;
}
