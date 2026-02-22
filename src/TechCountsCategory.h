#ifndef TECH_COUNTS_CATEGORY_H
#define TECH_COUNTS_CATEGORY_H

#include <string>

enum TechCountsCategory {
  TechCountsCategory_Crossovers = 0,
  TechCountsCategory_HalfCrossovers,
  TechCountsCategory_FullCrossovers,
  TechCountsCategory_Footswitches,
  TechCountsCategory_UpFootswitches,
  TechCountsCategory_DownFootswitches,
  TechCountsCategory_Sideswitches,
  TechCountsCategory_Jacks,
  TechCountsCategory_Brackets,
  TechCountsCategory_Doublesteps,
  NUM_TechCountsCategory,
  TechCountsCategory_Invalid
};

const std::string& TechCountsCategoryToString(TechCountsCategory tnst);
const std::string& TechCountsCategoryToLocalizedString(TechCountsCategory tnst);

#endif
