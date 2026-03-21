
#include "simpleinput.h"

#include "../cc-lib/util.h"
#include "tasbot.h"

using namespace std;

// Input line format: 5 chars "UDLRF" where . = not pressed.
// We also support 7-char lines "RSUDLRF" for console switches.

static uint8 ParseInputLine(const string &line) {
  // Try 5-char format first (just joystick: UDLRF)
  if (line.size() >= 5) {
    uint8 command = 0;
    int offset = 0;
    int num_buttons = 5;

    // If 7 chars, parse console switches too: RSUDLRF
    if (line.size() >= 7 && line[0] != '#' && line[0] != 's') {
      offset = 0;
      num_buttons = 7;
    } else {
      offset = 0;
      num_buttons = 5;
    }

    if (num_buttons == 7) {
      if (line[0] != '.') command |= INPUT_RESET;
      if (line[1] != '.') command |= INPUT_SELECT;
      if (line[2] != '.') command |= INPUT_U;
      if (line[3] != '.') command |= INPUT_D;
      if (line[4] != '.') command |= INPUT_L;
      if (line[5] != '.') command |= INPUT_R;
      if (line[6] != '.') command |= INPUT_F;
    } else {
      // 5-char: UDLRF
      if (line[0] != '.') command |= INPUT_U;
      if (line[1] != '.') command |= INPUT_D;
      if (line[2] != '.') command |= INPUT_L;
      if (line[3] != '.') command |= INPUT_R;
      if (line[4] != '.') command |= INPUT_F;
    }
    return command;
  }
  return 0;
}

vector<uint8> SimpleInput::ReadInputs(const string &filename) {
  vector<string> contents = Util::ReadFileToLines(filename);
  vector<uint8> out;
  for (int i = 0; i < (int)contents.size(); i++) {
    const string &line = contents[i];
    // Skip empty lines, comments, and header/subtitle lines.
    if (line.empty() || line[0] == '#') continue;
    if (line.size() > 9 && line.substr(0, 9) == "subtitle ") continue;

    // Must be an input line (5 or 7 chars of UDLRF or RSUDLRF).
    if (line.size() < 5) {
      fprintf(stderr, "Illegal input line: [%s]\n", line.c_str());
      abort();
    }

    out.push_back(ParseInputLine(line));
  }
  return out;
}

vector<uint8> SimpleInput::ReadInputsAndSubtitles(
    const string &filename,
    vector<string> *subtitles_out) {
  vector<string> contents = Util::ReadFileToLines(filename);

  // First pass: collect sparse subtitles.
  vector<pair<int, string>> sparse_subs;
  for (int i = 0; i < (int)contents.size(); i++) {
    const string &line = contents[i];
    if (line.size() > 9 && line.substr(0, 9) == "subtitle ") {
      size_t space1 = 9;
      size_t space2 = line.find(' ', space1);
      if (space2 != string::npos) {
        int frame = atoi(line.substr(space1, space2 - space1).c_str());
        string text = line.substr(space2 + 1);
        sparse_subs.emplace_back(frame, text);
      }
    }
  }

  // Second pass: read inputs.
  vector<uint8> out;
  for (int i = 0; i < (int)contents.size(); i++) {
    const string &line = contents[i];
    if (line.empty() || line[0] == '#') continue;
    if (line.size() > 9 && line.substr(0, 9) == "subtitle ") continue;
    if (line.size() < 5) {
      fprintf(stderr, "Illegal input line: [%s]\n", line.c_str());
      abort();
    }
    out.push_back(ParseInputLine(line));
  }

  // Expand sparse subtitles into dense array.
  if (subtitles_out) {
    subtitles_out->resize(out.size());
    size_t sub_idx = 0;
    string current_sub = "";
    for (size_t frame = 0; frame < out.size(); frame++) {
      while (sub_idx < sparse_subs.size() &&
             sparse_subs[sub_idx].first <= (int)frame) {
        current_sub = sparse_subs[sub_idx].second;
        sub_idx++;
      }
      (*subtitles_out)[frame] = current_sub;
    }
  }

  return out;
}

void SimpleInput::WriteInputs(const string &outputfile,
                               const string &romfilename,
                               const string &romchecksum,
                               const vector<uint8> &inputs) {
  vector<string> empty;
  WriteInputsWithSubtitles(outputfile, romfilename, romchecksum,
                           inputs, empty);
}

void SimpleInput::WriteInputsWithSubtitles(const string &outputfile,
                                            const string &romfilename,
                                            const string &romchecksum,
                                            const vector<uint8> &inputs,
                                            const vector<string> &subtitles) {
  FILE *f = fopen(outputfile.c_str(), "wb");
  fprintf(f,
          "# LearnfunPlayfun Atari 2600 input log\n"
          "# rom: %s\n"
          "# checksum: %s\n"
          "# frames: %d\n",
          romfilename.c_str(),
          romchecksum.c_str(),
          (int)inputs.size());

  const string *last = NULL;
  for (int i = 0; i < (int)subtitles.size(); i++) {
    if (last == NULL || *last != subtitles[i]) {
      fprintf(f, "subtitle %d %s\n", i, subtitles[i].c_str());
    }
    last = &subtitles[i];
  }

  static const char buttons[] = "UDLRF";
  static const int bits[] = { 4, 3, 2, 1, 0 };  // bit positions

  for (int i = 0; i < (int)inputs.size(); i++) {
    for (int j = 0; j < 5; j++) {
      fprintf(f, "%c",
              (inputs[i] & (1 << bits[j])) ? buttons[j] : '.');
    }
    fprintf(f, "\n");
  }
  fclose(f);
}

void SimpleInput::WriteInputsWithSubtitles(const string &outputfile,
                                            const string &romfilename,
                                            const string &romchecksum,
                                            const vector<uint8> &inputs,
                                            const vector<pair<int, string>> &subtitles) {
  FILE *f = fopen(outputfile.c_str(), "wb");
  fprintf(f,
          "# LearnfunPlayfun Atari 2600 input log\n"
          "# rom: %s\n"
          "# checksum: %s\n"
          "# frames: %d\n",
          romfilename.c_str(),
          romchecksum.c_str(),
          (int)inputs.size());

  for (const auto &sub : subtitles) {
    fprintf(f, "subtitle %d %s\n", sub.first, sub.second.c_str());
  }

  static const char buttons[] = "UDLRF";
  static const int bits[] = { 4, 3, 2, 1, 0 };

  for (int i = 0; i < (int)inputs.size(); i++) {
    for (int j = 0; j < 5; j++) {
      fprintf(f, "%c",
              (inputs[i] & (1 << bits[j])) ? buttons[j] : '.');
    }
    fprintf(f, "\n");
  }
  fclose(f);
}

vector<pair<int, string>> SimpleInput::MakeSparseSubtitles(
    const vector<string> &dense_subtitles) {
  vector<pair<int, string>> out;
  const string *last = nullptr;
  for (size_t i = 0; i < dense_subtitles.size(); i++) {
    if (last == nullptr || *last != dense_subtitles[i]) {
      out.emplace_back((int)i, dense_subtitles[i]);
    }
    last = &dense_subtitles[i];
  }
  return out;
}

string SimpleInput::InputToString(uint8 input) {
  char f[6] = {0};
  static const char buttons[] = "UDLRF";
  static const int bits[] = { 4, 3, 2, 1, 0 };
  for (int j = 0; j < 5; j++) {
    f[j] = (input & (1 << bits[j])) ? buttons[j] : '.';
  }
  return (string)f;
}

string SimpleInput::InputToColorString(uint8 input) {
  string color = "";
  string out;
  static const char DOTCOLOR[] = "#999";
  static const char buttons[] = "UDLRF";
  static const int bits[] = { 4, 3, 2, 1, 0 };
  static const char *colors[] = {
    "#000",    // U - direction
    "#000",    // D - direction
    "#000",    // L - direction
    "#000",    // R - direction
    "#900",    // F - fire (red)
  };
  for (int j = 0; j < 5; j++) {
    bool button_down = input & (1 << bits[j]);
    string this_color = button_down ? colors[j] : DOTCOLOR;
    char c = button_down ? buttons[j] : '.';
    if (color != this_color) {
      if (color != "") out += "</span>";
      out += "<span style=\"color:" + this_color + "\">";
      color = this_color;
    }
    out += StringPrintf("%c", c);
  }
  if (color != "") out += "</span>";
  return out;
}
