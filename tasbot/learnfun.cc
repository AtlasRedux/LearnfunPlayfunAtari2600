/* This program attempts to learn an objective function for a
   particular Atari 2600 game by watching input logs of people
   playing it. The objective function can then be used by
   playfun.exe to try to play the game.

   Ported from the NES/FCEUX version to Atari 2600 via libretro.
 */

#include "platform.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>

#include "tasbot.h"

#include "basis-util.h"
#include "emulator.h"
#include "types.h"
#include "simpleinput.h"
#include "objective.h"
#include "weighted-objectives.h"
#include "motifs.h"

// MARIONET networking is initialized in playfun, not here.

// deprecated
#define FASTFORWARD 0

static void SaveMemory(vector< vector<uint8> > *memories) {
  memories->resize(memories->size() + 1);
  Emulator::GetMemory(&memories->back());
}

static vector< vector<int> > *objectives = NULL;
static void PrintAndSave(const vector<int> &ordering) {
  for (int i = 0; i < (int)ordering.size(); i++) {
    printf("%d ", ordering[i]);
  }
  printf("\n");
  CHECK(objectives);
  objectives->push_back(ordering);
}

// With e.g. an divisor of 3, generate slices covering
// the first third, middle third, and last third.
static void GenerateNthSlices(int divisor, int num,
                              const vector< vector<uint8> > &memories,
                              Objective *obj) {
  const int onenth = memories.size() / divisor;
  for (int slicenum = 0; slicenum < divisor; slicenum++) {
    vector<int> look;
    int low = slicenum * onenth;
    for (int i = 0; i < onenth; i++) {
      look.push_back(low + i);
    }
    printf("For slice %d-%d:\n", low, low + onenth - 1);
    for (int i = 0; i < num; i++) {
      obj->EnumerateFull(look, PrintAndSave, 1, slicenum * 0xBEAD + i);
    }
  }
}

static void GenerateOccasional(int stride, int offsets, int num,
                               const vector< vector<uint8> > &memories,
                               Objective *obj) {
  for (int off = 0; off < offsets; off++) {
    vector<int> look;
    for (int start = off; start < (int)memories.size(); start += stride) {
      look.push_back(start);
    }
    printf("For occasional @%d (every %d):\n", off, stride);
    for (int i = 0; i < num; i++) {
      obj->EnumerateFull(look, PrintAndSave, 1, off * 0xF00D + i);
    }
  }
}

static void MakeObjectives(const string &game,
                           const vector< vector<uint8> > &memories) {
  printf("Now generating objectives.\n");
  objectives = new vector< vector<int> >;
  Objective obj(memories);

  for (int i = 0; i < 50; i++)
    obj.EnumerateFullAll(PrintAndSave, 1, i);

  GenerateNthSlices(10, 3, memories, &obj);

  GenerateOccasional(100, 10, 10, memories, &obj);
  GenerateOccasional(250, 10, 10, memories, &obj);
  GenerateOccasional(1000, 10, 1, memories, &obj);

  printf("There are %d objectives\n", (int)objectives->size());
  WeightedObjectives weighted(*objectives);
  printf("And %d example memories\n", (int)memories.size());
  weighted.WeightByExamples(memories);
  printf("And %d unique objectives\n", (int)weighted.Size());

  weighted.SaveToFile(game + ".objectives");

  weighted.SaveSVG(memories, game + ".svg");
  weighted.SaveLua(6, game + ".lua");
}

int main(int argc, char *argv[]) {
  map<string, string> config = Util::ReadFileToMap("config.txt");
  if (config.empty()) {
    fprintf(stderr, "You need a file called config.txt; please "
            "take a look at the README.\n");
    abort();
  }

  const string game = config["game"];
  const string moviename = config["movie"];
  // Support explicit ROM path; fall back to game + ".a26".
  string romfile = config["rom"];
  if (romfile.empty()) romfile = game + ".a26";

  CHECK(!game.empty());
  CHECK(!moviename.empty());

  Emulator::Initialize(romfile);
  vector<uint8> movie = SimpleInput::ReadInputs(moviename);
  CHECK(!movie.empty());

  vector< vector<uint8> > memories;
  memories.reserve(movie.size() + 1);
  vector<uint8> inputs;

  size_t start = 0;

  printf("Skipping frames without input.\n");
  bool saw_input = false;
  while ((start < FASTFORWARD && !saw_input) &&
         start < movie.size()) {
    if (movie[start] != 0) saw_input = true;
    Emulator::Step(movie[start]);
    start++;
  }

  CHECK(start != movie.size());

  printf("Skipped %d frames until first keypress/ffwd.\n"
         "Playing %d frames...\n", (int)start, (int)(movie.size() - start));

  SaveMemory(&memories);

  {
    vector<uint8> save;
    Emulator::SaveUncompressed(&save);
    printf("Save states are %d bytes.\n", (int)save.size());
  }

  uint64 time_start = time(NULL);
  for (int i = start; i < (int)movie.size(); i++) {
    if (i % 1000 == 0) {
      printf("  [% 3.1f%%] %d/%d\n",
             ((100.0 * i) / movie.size()), i, (int)movie.size());
    }
    Emulator::Step(movie[i]);
    inputs.push_back(movie[i]);
    SaveMemory(&memories);
  }
  uint64 time_end = time(NULL);

  printf("Recorded %d memories in %d sec.\n",
         (int)memories.size(),
         (int)(time_end - time_start));

  MakeObjectives(game, memories);
  Motifs motifs;
  motifs.AddInputs(inputs);
  motifs.SaveToFile(game + ".motifs");

  Emulator::Shutdown();

  return 0;
}
