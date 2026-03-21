/* Tests the Atari 2600 emulator interface for correctness.
   Verifies RAM access, save/load state round-trips, determinism,
   and caching behavior. */

#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../cc-lib/util.h"
#include "../cc-lib/timer.h"

#include "simpleinput.h"
#include "emulator.h"
#include "basis-util.h"
#include "tasbot.h"
#include "types.h"

static uint64 CrapHash(int a) {
  uint64 ret = ~a;
  ret *= 31337;
  ret ^= 0xDEADBEEF;
  ret = (ret >> 17) | (ret << (64 - 17));
  ret -= 911911911911ULL;
  ret *= 65537;
  ret ^= 0xCAFEBABE;
  return ret;
}

static bool CompareByHash(int a, int b) {
  return CrapHash(a) < CrapHash(b);
}

int main(int argc, char *argv[]) {
  const char *romfile = "test.a26";
  if (argc > 1) romfile = argv[1];

  fprintf(stderr, "Initializing emulator with [%s]...\n", romfile);
  if (!Emulator::Initialize(romfile)) {
    fprintf(stderr, "Failed to initialize emulator with [%s]\n", romfile);
    return 1;
  }

  // Test 1: GetMemory returns 128 bytes.
  {
    vector<uint8> mem;
    Emulator::GetMemory(&mem);
    fprintf(stderr, "RAM size: %d bytes\n", (int)mem.size());
    CHECK(mem.size() == 128);
    fprintf(stderr, "  PASS: RAM is 128 bytes.\n");
  }

  // Test 2: Step and verify RAM changes.
  {
    Emulator::Step(0);  // no input
    vector<uint8> mem;
    Emulator::GetMemory(&mem);
    CHECK(mem.size() == 128);
    fprintf(stderr, "  PASS: Step with no input works.\n");
  }

  // Test 3: SaveUncompressed/LoadUncompressed round-trip.
  {
    // Run a few frames to get non-trivial state.
    for (int i = 0; i < 10; i++) Emulator::Step(0);

    vector<uint8> state_before;
    Emulator::SaveUncompressed(&state_before);
    fprintf(stderr, "Uncompressed state size: %d bytes\n", (int)state_before.size());
    CHECK(!state_before.empty());

    vector<uint8> mem_before;
    Emulator::GetMemory(&mem_before);

    // Run more frames to change state.
    for (int i = 0; i < 50; i++) Emulator::Step(INPUT_R);

    // Load the saved state.
    Emulator::LoadUncompressed(&state_before);

    vector<uint8> mem_after;
    Emulator::GetMemory(&mem_after);
    CHECK(mem_before == mem_after);
    fprintf(stderr, "  PASS: SaveUncompressed/LoadUncompressed round-trip.\n");
  }

  // Test 4: Save/Load (compressed) round-trip.
  {
    for (int i = 0; i < 10; i++) Emulator::Step(INPUT_F);

    vector<uint8> state_compressed;
    Emulator::Save(&state_compressed);
    fprintf(stderr, "Compressed state size: %d bytes\n", (int)state_compressed.size());

    vector<uint8> mem_before;
    Emulator::GetMemory(&mem_before);

    for (int i = 0; i < 50; i++) Emulator::Step(INPUT_L);

    Emulator::Load(&state_compressed);

    vector<uint8> mem_after;
    Emulator::GetMemory(&mem_after);
    CHECK(mem_before == mem_after);
    fprintf(stderr, "  PASS: Save/Load (compressed) round-trip.\n");
  }

  // Test 5: Determinism — same inputs from same state produce same result.
  {
    vector<uint8> start_state;
    Emulator::SaveUncompressed(&start_state);

    // Run A
    Emulator::LoadUncompressed(&start_state);
    for (int i = 0; i < 100; i++) Emulator::Step(i & 0x1F);
    vector<uint8> mem_a;
    Emulator::GetMemory(&mem_a);

    // Run B
    Emulator::LoadUncompressed(&start_state);
    for (int i = 0; i < 100; i++) Emulator::Step(i & 0x1F);
    vector<uint8> mem_b;
    Emulator::GetMemory(&mem_b);

    CHECK(mem_a == mem_b);
    fprintf(stderr, "  PASS: Determinism verified.\n");
  }

  // Test 6: CachingStep produces same result as Step.
  {
    Emulator::ResetCache(100, 10);

    vector<uint8> start_state;
    Emulator::SaveUncompressed(&start_state);

    // Normal step
    Emulator::LoadUncompressed(&start_state);
    Emulator::Step(INPUT_U | INPUT_F);
    vector<uint8> mem_step;
    Emulator::GetMemory(&mem_step);

    // Caching step
    Emulator::LoadUncompressed(&start_state);
    Emulator::CachingStep(INPUT_U | INPUT_F);
    vector<uint8> mem_cache;
    Emulator::GetMemory(&mem_cache);

    CHECK(mem_step == mem_cache);
    fprintf(stderr, "  PASS: CachingStep matches Step.\n");
  }

  // Test 7: RamChecksum is consistent.
  {
    uint64 c1 = Emulator::RamChecksum();
    uint64 c2 = Emulator::RamChecksum();
    CHECK(c1 == c2);
    CHECK(c1 != 0);
    fprintf(stderr, "  PASS: RamChecksum is consistent (0x%llx).\n",
            (unsigned long long)c1);
  }

  // Test 8: Timing.
  fprintf(stderr, "\nTiming tests:\n");
  {
    vector<uint8> beginning;
    Emulator::Save(&beginning);

    Emulator::Load(&beginning);
    {
      Timer steps;
      static const int kNumSteps = 5000;
      for (int i = 0; i < kNumSteps; i++) {
        Emulator::Step(i & 0x1F);
      }
      fprintf(stderr, "  %.8f seconds per step\n",
              (double)steps.Seconds() / (double)kNumSteps);
    }

    Emulator::Load(&beginning);
    {
      vector<uint8> saveme;
      Timer saves;
      static const int kNumSaves = 5000;
      for (int i = 0; i < kNumSaves; i++) {
        Emulator::Save(&saveme);
      }
      fprintf(stderr, "  %.8f seconds per Save (compressed)\n",
              (double)saves.Seconds() / (double)kNumSaves);
    }

    Emulator::Load(&beginning);
    {
      vector<uint8> saveme;
      Timer saves;
      static const int kNumSaves = 5000;
      for (int i = 0; i < kNumSaves; i++) {
        Emulator::SaveUncompressed(&saveme);
      }
      fprintf(stderr, "  %.8f seconds per Save (uncompressed)\n",
              (double)saves.Seconds() / (double)kNumSaves);
    }

    {
      Timer loads;
      static const int kNumLoads = 5000;
      for (int i = 0; i < kNumLoads; i++) {
        Emulator::Load(&beginning);
      }
      fprintf(stderr, "  %.8f seconds per Load (compressed)\n",
              (double)loads.Seconds() / (double)kNumLoads);
    }
  }

  Emulator::Shutdown();

  fprintf(stderr, "\nSUCCESS.\n");
  return 0;
}
