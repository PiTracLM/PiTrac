/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "gs_results.h"

namespace golf_sim {

class GsOpenGolfSimResults : public GsResults {
public:
  GsOpenGolfSimResults() = default;
  explicit GsOpenGolfSimResults(const GsResults &results)
      : GsResults(results) {}
  std::string Format() const;
};

} // namespace golf_sim
