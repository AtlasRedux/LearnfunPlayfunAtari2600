/* Atari 2600 input format reader/writer.
   Replaces simplefm2.h for the NES version.

   File format (.a26inp):
     Lines starting with # are comments/headers.
     "subtitle <frame> <text>" lines are subtitles.
     Input lines are 5 characters: UDLRF
     where . means not pressed.
*/

#ifndef __SIMPLEINPUT_H
#define __SIMPLEINPUT_H

#include <vector>
#include <string>

#include "types.h"

// Input bit layout for Atari 2600 joystick.
// Bits from MSB to LSB: .RSUDLRF
#define INPUT_RESET  (1<<6)   // Console Reset switch
#define INPUT_SELECT (1<<5)   // Console Select switch
#define INPUT_U      (1<<4)   // Up
#define INPUT_D      (1<<3)   // Down
#define INPUT_L      (1<<2)   // Left
#define INPUT_R      (1<<1)   // Right
#define INPUT_F      (1<<0)   // Fire

using namespace std;

struct SimpleInput {
  static vector<uint8> ReadInputs(const string &filename);

  // Read inputs and subtitles together. Subtitles are returned as a
  // dense vector (one entry per input frame).
  static vector<uint8> ReadInputsAndSubtitles(
      const string &filename,
      vector<string> *subtitles_out);

  static void WriteInputs(const string &outputfile,
                           const string &romfilename,
                           const string &romchecksum,
                           const vector<uint8> &inputs);

  static void WriteInputsWithSubtitles(const string &outputfile,
                                        const string &romfilename,
                                        const string &romchecksum,
                                        const vector<uint8> &inputs,
                                        const vector<string> &subtitles);

  static void WriteInputsWithSubtitles(const string &outputfile,
                                        const string &romfilename,
                                        const string &romchecksum,
                                        const vector<uint8> &inputs,
                                        const vector<pair<int, string>> &subtitles);

  static vector<pair<int, string>> MakeSparseSubtitles(
      const vector<string> &dense_subtitles);

  static string InputToString(uint8 input);
  static string InputToColorString(uint8 input);
};

#endif
