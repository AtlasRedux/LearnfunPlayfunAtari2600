
#include "emulator.h"

#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <lz4.h>
#include <unordered_map>

#include "libretro.h"
#include "tasbot.h"
#include "../cc-lib/city/city.h"
#include "../cc-lib/util.h"

// Reuse DCHECK from tasbot.h if available; otherwise define it.
#ifndef DCHECK
#define DCHECK(x) do {} while(0)
#endif

// Current input state, set before retro_run() and read by the
// input_state callback.
static uint8 current_input = 0;
static bool initialized = false;

// Frame buffer storage (populated by video refresh callback).
static const void *fb_data = nullptr;
static unsigned fb_width = 0, fb_height = 0;
static size_t fb_pitch = 0;
static int fb_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

// Audio storage (populated by audio callbacks).
static vector<int16> audio_buffer;
// When true, skip audio accumulation (faster for search).
static bool collect_audio = false;

// Cached serialization size (queried once after load).
static size_t cached_serialize_size = 0;

// --- Libretro callbacks ---

static bool environment_cb(unsigned cmd, void *data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      fb_pixel_format = *(const int *)data;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
      *(const char **)data = ".";
      return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
      *(const char **)data = ".";
      return true;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
      *(bool *)data = true;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
      // Don't provide a log callback; the core will use fprintf.
      return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      // Return default for all variables.
      var->value = nullptr;
      return false;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      return false;
    default:
      return false;
  }
}

static void video_refresh_cb(const void *data, unsigned width,
                              unsigned height, size_t pitch) {
  fb_data = data;
  fb_width = width;
  fb_height = height;
  fb_pitch = pitch;
}

static void audio_sample_cb(int16_t left, int16_t right) {
  if (!collect_audio) return;
  audio_buffer.push_back((int16)((left + right) / 2));
}

static size_t audio_sample_batch_cb(const int16_t *data, size_t frames) {
  if (!collect_audio) return frames;
  for (size_t i = 0; i < frames; i++) {
    int16_t left = data[i * 2];
    int16_t right = data[i * 2 + 1];
    audio_buffer.push_back((int16)((left + right) / 2));
  }
  return frames;
}

static void input_poll_cb(void) {
  // No-op: we set current_input before retro_run().
}

static int16_t input_state_cb(unsigned port, unsigned device,
                               unsigned index, unsigned id) {
  if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
  switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_UP:     return (current_input >> 4) & 1;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (current_input >> 3) & 1;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (current_input >> 2) & 1;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (current_input >> 1) & 1;
    case RETRO_DEVICE_ID_JOYPAD_B:      return (current_input >> 0) & 1;  // Fire
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return (current_input >> 5) & 1;  // Console Select
    case RETRO_DEVICE_ID_JOYPAD_START:  return (current_input >> 6) & 1;  // Console Reset
    default: return 0;
  }
}

// --- State cache (optimized for large libretro states) ---
//
// Unlike the NES version which stored full state blobs as keys,
// we use a compact 64-bit hash of (input, state) as the key.
// The value stores only the result state.  This avoids hashing,
// comparing, and copying hundreds of KB per lookup.
// Tiny collision risk is acceptable for a search cache.

struct StateCache {
  // Key: hash of (input byte, serialized state).
  // Value: sequence number (for LRU), result state.
  struct Entry {
    uint64 sequence;
    vector<uint8> result;
  };

  typedef unordered_map<uint64, Entry> Hash;

  StateCache() : limit(0ULL), count(0ULL), next_sequence(0ULL),
                 slop(10000ULL), hits(0ULL), misses(0ULL) {
  }

  static uint64 MakeKey(uint8 input, const vector<uint8> &state) {
    return CityHash64WithSeed((const char *)state.data(),
                              state.size(), (uint64)input);
  }

  void Resize(uint64 ll, uint64 ss) {
    printf("Resize cache %llu %llu\n", (unsigned long long)ll, (unsigned long long)ss);
    hashtable.clear();
    limit = ll;
    slop = ss;
    next_sequence = count = 0ULL;
    printf("OK.\n");
  }

  void Remember(uint8 input, const vector<uint8> &start,
                vector<uint8> &&result) {
    uint64 key = MakeKey(input, start);
    Entry &e = hashtable[key];
    e.sequence = next_sequence++;
    e.result = std::move(result);
    count = hashtable.size();
    MaybeResize();
  }

  vector<uint8> *GetKnownResult(uint8 input, const vector<uint8> &start) {
    uint64 key = MakeKey(input, start);
    Hash::iterator it = hashtable.find(key);
    if (it == hashtable.end()) {
      misses++;
      return NULL;
    }

    hits++;
    it->second.sequence = next_sequence++;
    return &it->second.result;
  }

  void MaybeResize() {
    if (count > limit + slop) {
      uint64 num_to_remove = count - limit;

      vector<uint64> all_sequences;
      all_sequences.reserve(count);

      for (auto &kv : hashtable) {
        all_sequences.push_back(kv.second.sequence);
      }
      std::sort(all_sequences.begin(), all_sequences.end());

      if (num_to_remove < all_sequences.size()) {
        const uint64 minseq = all_sequences[(size_t)num_to_remove];

        for (auto it = hashtable.begin(); it != hashtable.end(); ) {
          if (it->second.sequence < minseq) {
            it = hashtable.erase(it);
          } else {
            ++it;
          }
        }
      }
      count = hashtable.size();
    }
  }

  void PrintStats() {
    printf("Current cache size: %llu / %llu. next_seq %llu\n"
           "%llu hits and %llu misses\n",
           (unsigned long long)count, (unsigned long long)limit,
           (unsigned long long)next_sequence,
           (unsigned long long)hits, (unsigned long long)misses);
  }

  Hash hashtable;
  uint64 limit;
  uint64 count;
  uint64 next_sequence;
  uint64 slop;
  uint64 hits, misses;
};
static StateCache *cache = NULL;

// --- Emulator implementation ---

void Emulator::GetMemory(vector<uint8> *mem) {
  uint8 *ram = (uint8 *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
  size_t sz = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  if (ram && sz > 0) {
    mem->resize(sz);
    memcpy(&((*mem)[0]), ram, sz);
  } else {
    mem->clear();
  }
}

uint64 Emulator::RamChecksum() {
  uint8 *ram = (uint8 *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
  size_t sz = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  if (!ram || sz == 0) return 0;
  return CityHash64((const char *)ram, sz);
}

bool Emulator::Initialize(const string &romfile) {
  if (initialized) {
    fprintf(stderr, "Already initialized.\n");
    abort();
    return false;
  }

  cache = new StateCache;

  fprintf(stderr, "Starting Atari 2600 emulator (stella2023 libretro)...\n");

  // Set callbacks before retro_init.
  retro_set_environment(environment_cb);
  retro_set_video_refresh(video_refresh_cb);
  retro_set_audio_sample(audio_sample_cb);
  retro_set_audio_sample_batch(audio_sample_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);

  retro_init();

  // Load the ROM.
  vector<uint8> romdata;
  {
    string contents = Util::ReadFile(romfile);
    if (contents.empty()) {
      fprintf(stderr, "Couldn't read ROM [%s]\n", romfile.c_str());
      return false;
    }
    romdata.assign(contents.begin(), contents.end());
  }

  struct retro_game_info game_info;
  memset(&game_info, 0, sizeof(game_info));
  game_info.path = romfile.c_str();
  game_info.data = romdata.data();
  game_info.size = romdata.size();
  game_info.meta = nullptr;

  if (!retro_load_game(&game_info)) {
    fprintf(stderr, "Couldn't load ROM [%s]\n", romfile.c_str());
    retro_deinit();
    return false;
  }

  // Set port 0 to joypad.
  retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

  initialized = true;

  // Run a few warmup frames so the core is fully initialized.
  // stella2023 can fail retro_serialize() if called before any
  // frames have been emulated, especially under concurrent launches.
  current_input = 0;
  collect_audio = false;
  for (int i = 0; i < 4; i++) {
    retro_run();
  }

  // Cache the serialize size — queried once, used on every save/load.
  cached_serialize_size = retro_serialize_size();
  fprintf(stderr, "State serialization size: %zu bytes (%.1f KB)\n",
          cached_serialize_size, cached_serialize_size / 1024.0);

  // Verify serialization works before handing back to caller.
  {
    vector<uint8> test(cached_serialize_size);
    if (!retro_serialize(test.data(), cached_serialize_size)) {
      fprintf(stderr, "WARNING: Initial serialize check failed, "
              "running more warmup frames...\n");
      for (int i = 0; i < 60; i++) retro_run();
      cached_serialize_size = retro_serialize_size();
      if (!retro_serialize(test.data(), cached_serialize_size)) {
        fprintf(stderr, "ERROR: retro_serialize still failing after warmup.\n");
        return false;
      }
    }
  }

  return true;
}

void Emulator::Shutdown() {
  if (initialized) {
    retro_unload_game();
    retro_deinit();
    initialized = false;
  }
}

void Emulator::Step(uint8 inputs) {
  current_input = inputs;
  // Don't collect audio during search — saves time.
  collect_audio = false;
  retro_run();
}

void Emulator::StepFull(uint8 inputs) {
  current_input = inputs;
  collect_audio = true;
  audio_buffer.clear();
  retro_run();
}

void Emulator::GetImage(vector<uint8> *rgba) {
  rgba->clear();
  if (!fb_data || fb_width == 0 || fb_height == 0) return;

  rgba->resize(fb_width * fb_height * 4);

  for (unsigned y = 0; y < fb_height; y++) {
    for (unsigned x = 0; x < fb_width; x++) {
      uint8 r, g, b;

      if (fb_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
        const uint32 *row = (const uint32 *)((const uint8 *)fb_data + y * fb_pitch);
        uint32 pixel = row[x];
        r = (pixel >> 16) & 0xFF;
        g = (pixel >> 8) & 0xFF;
        b = pixel & 0xFF;
      } else if (fb_pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
        const uint16 *row = (const uint16 *)((const uint8 *)fb_data + y * fb_pitch);
        uint16 pixel = row[x];
        r = (uint8)((pixel >> 11) << 3);
        g = (uint8)(((pixel >> 5) & 0x3F) << 2);
        b = (uint8)((pixel & 0x1F) << 3);
      } else {
        // RETRO_PIXEL_FORMAT_0RGB1555
        const uint16 *row = (const uint16 *)((const uint8 *)fb_data + y * fb_pitch);
        uint16 pixel = row[x];
        r = (uint8)((pixel >> 10) << 3);
        g = (uint8)(((pixel >> 5) & 0x1F) << 3);
        b = (uint8)((pixel & 0x1F) << 3);
      }

      size_t idx = (y * fb_width + x) * 4;
      (*rgba)[idx + 0] = r;
      (*rgba)[idx + 1] = g;
      (*rgba)[idx + 2] = b;
      (*rgba)[idx + 3] = 0xFF;
    }
  }
}

void Emulator::GetImageSize(int *width, int *height) {
  *width = (int)fb_width;
  *height = (int)fb_height;
}

float Emulator::GetAspectRatio() {
  struct retro_system_av_info av;
  retro_get_system_av_info(&av);
  return av.geometry.aspect_ratio;
}

double Emulator::GetFPS() {
  struct retro_system_av_info av;
  retro_get_system_av_info(&av);
  return av.timing.fps;
}

void Emulator::GetSound(vector<int16> *wav) {
  *wav = audio_buffer;
}

// --- Save/Load via libretro serialization ---

static void SerializeRaw(vector<uint8> *out) {
  // Use cached size to avoid querying the core every time.
  size_t sz = cached_serialize_size;
  if (sz == 0) {
    // Fallback: re-query if cached size is stale.
    sz = retro_serialize_size();
    cached_serialize_size = sz;
  }
  out->resize(sz);
  if (!retro_serialize(out->data(), sz)) {
    // Retry: run a frame and try again.  stella2023 occasionally
    // fails serialize under concurrent process launches.
    fprintf(stderr, "retro_serialize failed, retrying after a frame...\n");
    retro_run();
    sz = retro_serialize_size();
    cached_serialize_size = sz;
    out->resize(sz);
    if (!retro_serialize(out->data(), sz)) {
      fprintf(stderr, "retro_serialize failed after retry\n");
      abort();
    }
  }
}

static void DeserializeRaw(const vector<uint8> *in) {
  if (!retro_unserialize(in->data(), in->size())) {
    fprintf(stderr, "retro_unserialize failed\n");
    abort();
  }
}

void Emulator::GetBasis(vector<uint8> *out) {
  SerializeRaw(out);
}

void Emulator::SaveUncompressed(vector<uint8> *out) {
  SerializeRaw(out);
}

void Emulator::LoadUncompressed(vector<uint8> *in) {
  DeserializeRaw(in);
}

void Emulator::Save(vector<uint8> *out) {
  SaveEx(out, NULL);
}

void Emulator::Load(vector<uint8> *state) {
  LoadEx(state, NULL);
}

void Emulator::SaveEx(vector<uint8> *state, const vector<uint8> *basis) {
  vector<uint8> raw;
  SerializeRaw(&raw);

  // Delta-encode against basis.
  int blen = (basis == NULL) ? 0 : (int)(min(basis->size(), raw.size()));
  for (int i = 0; i < blen; i++) {
    raw[i] -= (*basis)[i];
  }

  // Compress with LZ4.
  int len = (int)raw.size();
  int maxcomprlen = LZ4_compressBound(len);

  // Header: 4 bytes uncompressed length + 4 bytes compressed length.
  state->resize(8 + maxcomprlen);

  int comprlen = LZ4_compress_default(
      (const char *)&raw[0], (char *)&(*state)[8],
      len, maxcomprlen);
  if (comprlen <= 0) {
    fprintf(stderr, "Couldn't compress.\n");
    abort();
  }

  *(uint32*)&(*state)[0] = (uint32)len;
  *(uint32*)&(*state)[4] = (uint32)comprlen;

  // Trim to actual compressed size.
  state->resize(8 + comprlen);
}

void Emulator::LoadEx(vector<uint8> *state, const vector<uint8> *basis) {
  // Decompress LZ4.
  int uncomprlen = (int)*(uint32*)&(*state)[0];
  int comprlen = (int)*(uint32*)&(*state)[4];
  vector<uint8> uncompressed;
  uncompressed.resize(uncomprlen);

  int result = LZ4_decompress_safe(
      (const char *)&(*state)[8], (char *)&uncompressed[0],
      comprlen, uncomprlen);
  if (result < 0) {
    fprintf(stderr, "LZ4 decompression error: %d\n", result);
    abort();
  }
  uncompressed.resize(result);

  // Delta-decode against basis.
  int blen = (basis == NULL) ? 0 : (int)(min(basis->size(), uncompressed.size()));
  for (int i = 0; i < blen; i++) {
    uncompressed[i] += (*basis)[i];
  }

  DeserializeRaw(&uncompressed);
}

// --- Cache ---

void Emulator::ResetCache(uint64 numstates, uint64 slop) {
  CHECK(cache != NULL);
  cache->Resize(numstates, slop);
}

void Emulator::CachingStep(uint8 input) {
  vector<uint8> start;
  SaveUncompressed(&start);
  if (vector<uint8> *cached = cache->GetKnownResult(input, start)) {
    LoadUncompressed(cached);
  } else {
    Step(input);
    vector<uint8> result;
    SaveUncompressed(&result);
    cache->Remember(input, start, std::move(result));
  }
}

void Emulator::PrintCacheStats() {
  CHECK(cache != NULL);
  cache->PrintStats();
}
