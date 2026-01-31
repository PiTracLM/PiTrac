/* SPDX-License-Identifier: GPL-2.0-only */

#include <cmath>
#include <cstdio>
#include <string>

#include "gs_opengolfsim_results.h"

namespace golf_sim {

std::string GsOpenGolfSimResults::Format() const {
  // Don't ever send keepalives as shots
  if (result_message_is_keepalive_)
    return "";

  // Basic sanity: ignore false triggers / mis-detections with near-zero speed
  if (!std::isfinite(speed_mph_) || speed_mph_ < 3.0)
    return "";

  // Compute total spin magnitude from components
  const double backSpinRpm = back_spin_rpm_;
  const double sideSpinRpm = side_spin_rpm_;
  const double spinSpeedRpm =
      std::sqrt(backSpinRpm * backSpinRpm + sideSpinRpm * sideSpinRpm);

  // "20% above normal high wedge spin" filter (12,000 * 1.2 = 14,400)
  constexpr double kMaxSpinRpm = 14400.0;
  if (!std::isfinite(spinSpeedRpm) || spinSpeedRpm > kMaxSpinRpm)
    return "";

  // Map PiTrac fields to OpenGolfSim fields
  const double ballSpeedMph = speed_mph_;
  const double vLaunchDeg = vla_deg_;
  const double hLaunchDeg = hla_deg_;
  const double spinAxisDeg = GetSpinAxis();

  // Optional: extra sanity on angles/axis
  if (!std::isfinite(vLaunchDeg) || !std::isfinite(hLaunchDeg) ||
      !std::isfinite(spinAxisDeg))
    return "";

  char buf[512];
  std::snprintf(
      buf, sizeof(buf),
      R"({"type":"shot","shot":{"ballSpeed":%.3f,"verticalLaunchAngle":%.3f,"horizontalLaunchAngle":%.3f,"spinAxis":%.3f,"spinSpeed":%.3f}})",
      ballSpeedMph, vLaunchDeg, hLaunchDeg, spinAxisDeg, spinSpeedRpm);

  return std::string(buf);
}

} // namespace golf_sim
