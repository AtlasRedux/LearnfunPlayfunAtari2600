/*
  Library interface to Atari 2600 via libretro (stella2023).

  Singleton wrapper — the libretro core uses global state,
  so only one instance can exist at a time.
*/

#ifndef __EMULATOR_H
#define __EMULATOR_H

#include <vector>
#include <string>

#include "types.h"

using namespace std;

struct Emulator {
  // Returns false upon error. Only initialize once.
  static bool Initialize(const string &romfile);
  static void Shutdown();

  static void Save(vector<uint8> *out);
  // Doesn't modify its argument.
  static void Load(vector<uint8> *in);

  // Make one emulator step with the given input.
  // Bits from MSB to LSB are:
  //    ..RSUDLRF
  //    bit 6: Reset (console switch)
  //    bit 5: Select (console switch)
  //    bit 4: Up
  //    bit 3: Down
  //    bit 2: Left
  //    bit 1: Right
  //    bit 0: Fire
  //
  // Consider CachingStep if you are doing search and might
  // execute this same step again.
  static void Step(uint8 inputs);

  // Copy the 128 bytes (0x80) of Atari 2600 RAM.
  static void GetMemory(vector<uint8> *mem);

  // Same as Step, but ensures video/audio callbacks fire.
  // libretro always renders, so this is equivalent to Step().
  static void StepFull(uint8 inputs);

  // Get image. StepFull must have been called to produce this frame.
  // 4 color channels (bytes) per pixel in RGBA order.
  static void GetImage(vector<uint8> *rgba);

  // Get the dimensions of the last rendered frame.
  static void GetImageSize(int *width, int *height);

  // Get the display aspect ratio reported by the core (e.g. 4:3).
  // Returns 0.0 if unknown (caller should assume 4:3).
  static float GetAspectRatio();

  // Get the core's reported FPS (typically ~60 for NTSC).
  static double GetFPS();

  // Get sound. StepFull must have been called to produce this wave.
  // The result is a vector of signed 16-bit samples, mono.
  static void GetSound(vector<int16> *wav);

  // Returns 64-bit checksum of RAM (only).
  static uint64 RamChecksum();

  // Reset the state cache.
  static void ResetCache(uint64 numstates, uint64 slop = 10000ULL);

  // Equivalent to Step but consults and populates a cache.
  static void CachingStep(uint8 input);

  static void PrintCacheStats();

  // Get an uncompressed basis state for delta compression.
  static void GetBasis(vector<uint8> *out);

  // Save and load uncompressed (fastest, but largest).
  static void SaveUncompressed(vector<uint8> *out);
  static void LoadUncompressed(vector<uint8> *in);

  // Save and load with a basis vector for delta compression.
  static void SaveEx(vector<uint8> *out, const vector<uint8> *basis);
  static void LoadEx(vector<uint8> *in, const vector<uint8> *basis);
};


#endif
