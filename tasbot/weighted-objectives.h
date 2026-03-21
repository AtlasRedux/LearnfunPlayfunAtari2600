/* A set of objective functions which may be weighted, and which may
   carry observations that allow them to be scored in absolute terms. */

#ifndef __WEIGHTED_OBJECTIVES_H
#define __WEIGHTED_OBJECTIVES_H

#include <map>
#include <vector>
#include <string>
#include <utility>

#include "tasbot.h"
#include "types.h"

struct WeightedObjectives {
  explicit WeightedObjectives(const std::vector< vector<int> > &objs);
  static WeightedObjectives *LoadFromFile(const std::string &filename);

  void WeightByExamples(const vector< vector<uint8> > &memories);

  // Does not save observations.
  void SaveToFile(const std::string &filename) const;

  // XXX version that uses observations?
  void SaveSVG(const vector< vector<uint8> > &memories,
               const string &filename) const;

  // More diagnostics. Only show the n highest-scoring objectives.
  void SaveLua(int n, const std::string &filename) const;

  size_t Size() const;

  // Scoring function which is just the sum of the weights of
  // objectives where mem1 < mem2.
  double WeightedLess(const vector<uint8> &mem1,
                      const vector<uint8> &mem2) const;

  // Scoring function which is the count of objectives
  // that where mem1 < mem2 minus the number where mem1 > mem2.
  double Evaluate(const vector<uint8> &mem1,
                  const vector<uint8> &mem2) const;

  void Observe(const vector<uint8> &memory);

  double GetNormalizedValue(const vector<uint8> &memory);

  vector<double> GetNormalizedValues(const vector<uint8> &memory);

  std::vector< std::pair<const std::vector<int> *, double> > GetAll() const;

 private:
  WeightedObjectives();
  struct Info;
  typedef std::map< std::vector<int>, Info* > Weighted;
  Weighted weighted;

  NOT_COPYABLE(WeightedObjectives);
};

#endif
