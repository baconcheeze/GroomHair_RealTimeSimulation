#pragma once
#include "Layout.h"
namespace gpuls {
int codec(const fs::path&,const fs::path&);
int ownership(Context &, const fs::path &, const fs::path &, unsigned tile,
              unsigned onlyGroup);
int launchStudy(Context &, const fs::path &, const fs::path &);
} // namespace gpuls
