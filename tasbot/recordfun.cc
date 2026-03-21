/* Interactive Atari 2600 gameplay recorder.

   Opens a window showing the game running via the libretro core.
   The player uses keyboard to control the joystick.
   On exit (ESC or close window), the inputs are saved as .a26inp.

   Keyboard mapping:
     Arrow keys   = Up / Down / Left / Right
     Z or Space   = Fire
     Enter        = Console Reset
     Tab          = Console Select
     ESC          = Quit and save

   Usage:
     recordfun.exe

   Reads config.txt for:
     game   - base name (used for output filename)
     rom    - ROM filename (optional, defaults to game.a26)

   Output: <game>.a26inp
*/

#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>

#include <SDL.h>

#include "tasbot.h"
#include "emulator.h"
#include "types.h"
#include "simpleinput.h"
#include "util.h"

// Scale factor for the display window.
static const int SCALE = 3;

int main(int argc, char *argv[]) {
  map<string, string> config = Util::ReadFileToMap("config.txt");
  if (config.empty()) {
    fprintf(stderr, "You need a file called config.txt; please "
            "take a look at the README.\n");
    abort();
  }

  const string game = config["game"];
  CHECK(!game.empty());

  string romfile = config["rom"];
  if (romfile.empty()) romfile = game + ".a26";

  fprintf(stderr, "Recording gameplay for: %s\n", romfile.c_str());

  // Initialize emulator.
  Emulator::Initialize(romfile);

  // Run one frame to get the video dimensions.
  Emulator::StepFull(0);
  vector<uint8> rgba;
  Emulator::GetImage(&rgba);

  int emu_w, emu_h;
  Emulator::GetImageSize(&emu_w, &emu_h);
  if (emu_w <= 0 || emu_h <= 0) {
    emu_w = 160;
    emu_h = 210;
  }

  // Use the core's aspect ratio to compute the correct display width.
  float aspect = Emulator::GetAspectRatio();
  if (aspect <= 0.0f) aspect = 4.0f / 3.0f;

  double fps = Emulator::GetFPS();
  if (fps <= 0.0) fps = 60.0;

  // Display height is emu_h * SCALE; width derived from aspect ratio.
  int win_h = emu_h * SCALE;
  int win_w = (int)(win_h * aspect + 0.5);

  fprintf(stderr, "Emulator video: %d x %d  aspect: %.3f  fps: %.2f\n",
          emu_w, emu_h, aspect, fps);
  fprintf(stderr, "Window: %d x %d\n", win_w, win_h);

  // Initialize SDL.
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    abort();
  }

  SDL_Window *window = SDL_CreateWindow(
      "TASBot Atari - Record",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      win_w, win_h,
      SDL_WINDOW_SHOWN);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    abort();
  }

  // Don't use PRESENTVSYNC — we do our own 60fps timing so the
  // emulation speed is correct regardless of monitor refresh rate.
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    abort();
  }

  SDL_Texture *texture = SDL_CreateTexture(
      renderer,
      SDL_PIXELFORMAT_ABGR8888,  // RGBA byte order
      SDL_TEXTUREACCESS_STREAMING,
      emu_w, emu_h);
  if (!texture) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    abort();
  }

  vector<uint8> inputs;
  bool running = true;
  int frame = 0;
  const double FRAME_MS = 1000.0 / fps;

  fprintf(stderr,
    "\n=== CONTROLS ===\n"
    "  Arrow keys : Move\n"
    "  Z / Space  : Fire\n"
    "  Enter      : Console Reset\n"
    "  Tab        : Console Select\n"
    "  ESC        : Stop and save\n"
    "================\n\n");

  while (running) {
    Uint64 frame_start = SDL_GetPerformanceCounter();

    // Poll SDL events.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE) {
        running = false;
      }
    }

    // Read current keyboard state.
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    uint8 input = 0;
    if (keys[SDL_SCANCODE_UP])     input |= INPUT_U;
    if (keys[SDL_SCANCODE_DOWN])   input |= INPUT_D;
    if (keys[SDL_SCANCODE_LEFT])   input |= INPUT_L;
    if (keys[SDL_SCANCODE_RIGHT])  input |= INPUT_R;
    if (keys[SDL_SCANCODE_Z] ||
        keys[SDL_SCANCODE_SPACE])  input |= INPUT_F;
    if (keys[SDL_SCANCODE_RETURN]) input |= INPUT_RESET;
    if (keys[SDL_SCANCODE_TAB])    input |= INPUT_SELECT;

    // Don't allow U+D or L+R simultaneously.
    if ((input & INPUT_U) && (input & INPUT_D))
      input &= ~INPUT_D;
    if ((input & INPUT_L) && (input & INPUT_R))
      input &= ~INPUT_R;

    // Step emulator.
    Emulator::StepFull(input);
    inputs.push_back(input);

    // Get image and display.
    Emulator::GetImage(&rgba);
    if (!rgba.empty()) {
      SDL_UpdateTexture(texture, NULL, rgba.data(), emu_w * 4);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, NULL, NULL);
      SDL_RenderPresent(renderer);
    }

    frame++;
    if (frame % 600 == 0) {
      fprintf(stderr, "Frame %d (%.1f seconds)\n", frame, frame / 60.0);
    }

    // Enforce 60fps timing.
    Uint64 frame_end = SDL_GetPerformanceCounter();
    double elapsed_ms = (double)(frame_end - frame_start) /
                        (double)SDL_GetPerformanceFrequency() * 1000.0;
    double sleep_ms = FRAME_MS - elapsed_ms;
    if (sleep_ms > 1.0) {
      SDL_Delay((Uint32)(sleep_ms));
    }
  }

  // Save the recording.
  string outfile = game + ".a26inp";
  char checksum_str[32];
  snprintf(checksum_str, sizeof(checksum_str), "%016llx",
           (unsigned long long)Emulator::RamChecksum());

  SimpleInput::WriteInputs(outfile, romfile, checksum_str, inputs);
  fprintf(stderr, "\nSaved %d frames (%.1f seconds) to %s\n",
          (int)inputs.size(), inputs.size() / 60.0, outfile.c_str());

  // Cleanup.
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  Emulator::Shutdown();

  return 0;
}
