/* Replay an .a26inp input log visually with audio.

   Reads a COPY of the input log (so the original isn't locked),
   feeds each frame to the libretro emulator, and displays the
   result in an SDL2 window with sound.

   Keyboard controls during replay:
     Space      = Pause / unpause
     Right      = Step one frame (while paused)
     Up/Down    = Speed up / slow down (1x–8x)
     R          = Restart from beginning
     M          = Mute / unmute audio
     ESC        = Quit

   Usage:
     replayfun.exe [input.a26inp]

   If no argument is given, reads config.txt for the game name
   and looks for <game>-playfun-futures-progress.a26inp,
   then falls back to <game>.a26inp.
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

// --- Audio ring buffer for SDL callback ---
// SDL's audio callback pulls from this ring buffer, which the main
// loop fills each frame from Emulator::GetSound().
static const int AUDIO_RING_SIZE = 65536;
static int16 audio_ring[AUDIO_RING_SIZE];
static volatile int audio_ring_read = 0;
static volatile int audio_ring_write = 0;
static bool audio_muted = false;

static void sdl_audio_callback(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  int16 *out = (int16 *)stream;
  int samples = len / 2;  // 16-bit mono
  for (int i = 0; i < samples; i++) {
    if (audio_ring_read != audio_ring_write && !audio_muted) {
      out[i] = audio_ring[audio_ring_read % AUDIO_RING_SIZE];
      audio_ring_read++;
    } else {
      out[i] = 0;  // silence when buffer is empty or muted
    }
  }
}

static void push_audio(const vector<int16> &wav) {
  for (size_t i = 0; i < wav.size(); i++) {
    // Drop samples if ring is full (better than blocking).
    int next = (audio_ring_write + 1);
    if (next - audio_ring_read >= AUDIO_RING_SIZE) continue;
    audio_ring[audio_ring_write % AUDIO_RING_SIZE] = wav[i];
    audio_ring_write++;
  }
}

// Copy a file to a temp path so the original isn't locked during replay.
static string CopyToTemp(const string &src) {
  string tmp = src + ".replay_tmp";
  vector<uint8> data;
  // Read entire file.
  FILE *f = fopen(src.c_str(), "rb");
  if (!f) return src;  // fallback: use original
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  data.resize(sz);
  fread(data.data(), 1, sz, f);
  fclose(f);
  // Write temp copy.
  f = fopen(tmp.c_str(), "wb");
  if (!f) return src;
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return tmp;
}

static const int SCALE = 3;

int main(int argc, char *argv[]) {
  map<string, string> config = Util::ReadFileToMap("config.txt");

  string romfile, inpfile;

  if (argc >= 2) {
    inpfile = argv[1];
  }

  // Resolve ROM and input file from config if not given on command line.
  string game;
  if (!config.empty()) {
    game = config["game"];
    romfile = config["rom"];
    if (romfile.empty() && !game.empty()) romfile = game + ".a26";
  }

  if (inpfile.empty() && !game.empty()) {
    // Try playfun's progress file first, then the base recording.
    string progress = game + "-playfun-futures-progress.a26inp";
    FILE *f = fopen(progress.c_str(), "rb");
    if (f) {
      fclose(f);
      inpfile = progress;
    } else {
      inpfile = game + ".a26inp";
    }
  }

  if (romfile.empty() || inpfile.empty()) {
    fprintf(stderr,
      "Usage: replayfun.exe [input.a26inp]\n"
      "  Or create a config.txt with 'game' and 'rom' fields.\n");
    return 1;
  }

  fprintf(stderr, "ROM:   %s\n", romfile.c_str());
  fprintf(stderr, "Input: %s\n", inpfile.c_str());

  // Copy input file so the original isn't locked during replay.
  string tmpfile = CopyToTemp(inpfile);
  fprintf(stderr, "Reading from temp copy: %s\n", tmpfile.c_str());

  // Read the input log from the copy.
  vector<uint8> inputs = SimpleInput::ReadInputs(tmpfile);
  if (inputs.empty()) {
    fprintf(stderr, "No inputs found in %s\n", inpfile.c_str());
    return 1;
  }
  fprintf(stderr, "Loaded %d frames (%.1f seconds at 60fps)\n",
          (int)inputs.size(), inputs.size() / 60.0);

  // Initialize emulator.
  Emulator::Initialize(romfile);

  // Save initial state so we can restart.
  vector<uint8> initial_state;
  Emulator::SaveUncompressed(&initial_state);

  // Run one frame to get video dimensions.
  Emulator::StepFull(0);
  vector<uint8> rgba;
  Emulator::GetImage(&rgba);

  int emu_w, emu_h;
  Emulator::GetImageSize(&emu_w, &emu_h);
  if (emu_w <= 0 || emu_h <= 0) {
    emu_w = 160;
    emu_h = 210;
  }

  float aspect = Emulator::GetAspectRatio();
  if (aspect <= 0.0f) aspect = 4.0f / 3.0f;

  double fps = Emulator::GetFPS();
  if (fps <= 0.0) fps = 60.0;

  int win_h = emu_h * SCALE;
  int win_w = (int)(win_h * aspect + 0.5);

  // Restore to initial state (that test frame shouldn't count).
  Emulator::LoadUncompressed(&initial_state);

  fprintf(stderr, "Video: %d x %d  aspect: %.3f  fps: %.2f\n",
          emu_w, emu_h, aspect, fps);
  fprintf(stderr, "Window: %d x %d\n", win_w, win_h);

  // Initialize SDL with video and audio.
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // Set up SDL audio output (mono, 16-bit signed, at the core's sample rate).
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 31400;  // stella2023 outputs ~31400 Hz
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 1024;
  want.callback = sdl_audio_callback;
  SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(
      NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (audio_dev > 0) {
    fprintf(stderr, "Audio: %d Hz, %d ch, buffer %d\n",
            have.freq, have.channels, have.samples);
    SDL_PauseAudioDevice(audio_dev, 0);  // start playing
  } else {
    fprintf(stderr, "Warning: no audio device: %s\n", SDL_GetError());
  }

  SDL_Window *window = SDL_CreateWindow(
      "TASBot Atari - Replay",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      win_w, win_h,
      SDL_WINDOW_SHOWN);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Texture *texture = SDL_CreateTexture(
      renderer,
      SDL_PIXELFORMAT_ABGR8888,
      SDL_TEXTUREACCESS_STREAMING,
      emu_w, emu_h);
  if (!texture) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    return 1;
  }

  bool running = true;
  bool paused = false;
  int frame = 0;
  int speed = 1;  // 1x, 2x, 4x, 8x
  int total_frames = (int)inputs.size();
  const double FRAME_MS = 1000.0 / fps;

  // Update window title with status.
  auto update_title = [&]() {
    char title[256];
    snprintf(title, sizeof(title),
             "TASBot Atari - Replay [%d/%d] %s %dx%s",
             frame, total_frames,
             paused ? "PAUSED" : "PLAYING", speed,
             audio_muted ? " [MUTED]" : "");
    SDL_SetWindowTitle(window, title);
  };

  fprintf(stderr,
    "\n=== REPLAY CONTROLS ===\n"
    "  Space      : Pause / unpause\n"
    "  Right      : Step one frame (when paused)\n"
    "  Up / Down  : Speed up / slow down\n"
    "  R          : Restart\n"
    "  M          : Mute / unmute audio\n"
    "  ESC        : Quit\n"
    "========================\n\n");

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            running = false;
            break;
          case SDLK_SPACE:
            paused = !paused;
            update_title();
            break;
          case SDLK_RIGHT:
            // Step one frame while paused.
            if (paused && frame < total_frames) {
              Emulator::StepFull(inputs[frame]);
              frame++;
              { vector<int16> wav; Emulator::GetSound(&wav); push_audio(wav); }
              Emulator::GetImage(&rgba);
              if (!rgba.empty()) {
                SDL_UpdateTexture(texture, NULL, rgba.data(), emu_w * 4);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);

                const int BAR_H = 4;
                float p = (total_frames > 0)
                    ? (float)frame / (float)total_frames : 0.0f;
                if (p > 1.0f) p = 1.0f;
                SDL_Rect bg2 = { 0, win_h - BAR_H, win_w, BAR_H };
                SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
                SDL_RenderFillRect(renderer, &bg2);
                SDL_Rect fg2 = { 0, win_h - BAR_H, (int)(win_w * p), BAR_H };
                SDL_SetRenderDrawColor(renderer, 80, 220, 80, 255);
                SDL_RenderFillRect(renderer, &fg2);

                SDL_RenderPresent(renderer);
              }
              update_title();
            }
            break;
          case SDLK_UP:
            if (speed < 8) speed *= 2;
            update_title();
            break;
          case SDLK_DOWN:
            if (speed > 1) speed /= 2;
            update_title();
            break;
          case SDLK_m:
            audio_muted = !audio_muted;
            fprintf(stderr, "Audio %s\n", audio_muted ? "MUTED" : "ON");
            update_title();
            break;
          case SDLK_r:
            // Restart.
            Emulator::LoadUncompressed(&initial_state);
            audio_ring_read = audio_ring_write = 0;
            frame = 0;
            paused = false;
            update_title();
            break;
        }
      }
    }

    if (paused || frame >= total_frames) {
      SDL_Delay(16);
      if (frame >= total_frames && !paused) {
        paused = true;
        update_title();
        fprintf(stderr, "Replay complete (%d frames).\n", total_frames);
      }
      continue;
    }

    Uint64 frame_start = SDL_GetPerformanceCounter();

    // Run 'speed' frames per display frame.
    for (int s = 0; s < speed && frame < total_frames; s++) {
      Emulator::StepFull(inputs[frame]);
      frame++;
      // Push audio from each frame to the ring buffer.
      vector<int16> wav;
      Emulator::GetSound(&wav);
      push_audio(wav);
    }

    // Display.
    Emulator::GetImage(&rgba);
    if (!rgba.empty()) {
      SDL_UpdateTexture(texture, NULL, rgba.data(), emu_w * 4);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, NULL, NULL);

      // Progress bar at the bottom of the window.
      const int BAR_H = 4;
      float progress = (total_frames > 0)
          ? (float)frame / (float)total_frames : 0.0f;
      if (progress > 1.0f) progress = 1.0f;

      SDL_Rect bg = { 0, win_h - BAR_H, win_w, BAR_H };
      SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
      SDL_RenderFillRect(renderer, &bg);

      SDL_Rect fg = { 0, win_h - BAR_H, (int)(win_w * progress), BAR_H };
      SDL_SetRenderDrawColor(renderer, 80, 220, 80, 255);
      SDL_RenderFillRect(renderer, &fg);

      SDL_RenderPresent(renderer);
    }

    if (frame % 600 == 0) {
      update_title();
      fprintf(stderr, "Frame %d / %d (%.1fs / %.1fs)\n",
              frame, total_frames, frame / 60.0, total_frames / 60.0);
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

  if (audio_dev > 0) {
    SDL_PauseAudioDevice(audio_dev, 1);
    SDL_CloseAudioDevice(audio_dev);
  }
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  Emulator::Shutdown();

  // Clean up temp file.
  if (tmpfile != inpfile) {
    remove(tmpfile.c_str());
  }

  return 0;
}
