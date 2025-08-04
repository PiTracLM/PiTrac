#ifndef GS_TRAJECTORY_CALC_H
#define GS_TRAJECTORY_CALC_H

#include <optional>
#include <string>
#include <vector>
#include <array>

/**
 * PiTrac Trajectory Calculation Module
 * 
 * Provides carry distance calculation using libshotscope physics library
 * based on Prof. Alan Nathan's validated golf ball aerodynamics research.
 * 
 * Designed to be extensible for future atmospheric sensor additions.
 */

struct TrajectoryInput {
    // Current PiTrac measurements (required)
    double initial_velocity_mph;
    double vertical_launch_angle_deg;
    double horizontal_launch_angle_deg;
    double backspin_rpm;
    double sidespin_rpm;
    
    // Future atmospheric sensor extensions (optional)
    std::optional<double> temperature_f;
    std::optional<double> elevation_ft;
    std::optional<double> wind_speed_mph;
    std::optional<double> wind_direction_deg;
    std::optional<double> humidity_percent;
    std::optional<double> pressure_inhg;
};

struct TrajectoryResult {
    double carry_distance_yards;
    double flight_time_seconds;
    double landing_angle_deg;
    double max_height_yards;
    bool calculation_successful;
    std::string error_message;
};

class PiTracTrajectoryCalculator {
public:
    PiTracTrajectoryCalculator();
    ~PiTracTrajectoryCalculator();
    
    /**
     * Calculate carry distance using current PiTrac measurements
     * @param input Ball flight parameters from PiTrac
     * @return Trajectory results including carry distance
     */
    TrajectoryResult calculateCarry(const TrajectoryInput& input);
    
    /**
     * Get full trajectory path for visualization (future use)
     * @param input Ball flight parameters from PiTrac
     * @return Vector of 3D positions throughout flight
     */
    std::vector<std::array<double, 3>> calculateFullTrajectory(const TrajectoryInput& input);
    
    /**
     * Validate input parameters are within realistic ranges
     * @param input Parameters to validate
     * @return True if parameters are valid, false otherwise
     */
    bool validateInput(const TrajectoryInput& input);
    
private:
    // Default atmospheric conditions (sea level, 70°F, no wind)
    static constexpr double DEFAULT_TEMPERATURE_F = 70.0;
    static constexpr double DEFAULT_ELEVATION_FT = 0.0;
    static constexpr double DEFAULT_WIND_SPEED_MPH = 0.0;
    static constexpr double DEFAULT_WIND_DIRECTION_DEG = 0.0;
    static constexpr double DEFAULT_HUMIDITY_PERCENT = 50.0;
    static constexpr double DEFAULT_PRESSURE_INHG = 29.92;
    
    // Input validation ranges
    static constexpr double MIN_VELOCITY_MPH = 50.0;
    static constexpr double MAX_VELOCITY_MPH = 250.0;
    static constexpr double MIN_LAUNCH_ANGLE_DEG = -10.0;
    static constexpr double MAX_LAUNCH_ANGLE_DEG = 60.0;
    static constexpr double MAX_SPIN_RPM = 10000.0;
    
    /**
     * Convert PiTrac input to libshotscope format
     * @param input PiTrac trajectory input
     * @return libshotscope compatible structures
     */
    std::pair<void*, void*> convertToLibshotscopeFormat(const TrajectoryInput& input);
    
    /**
     * Apply default atmospheric conditions for missing sensor data
     * @param input Input with potentially missing atmospheric data
     * @return Complete atmospheric conditions
     */
    TrajectoryInput applyDefaults(const TrajectoryInput& input);
};

#endif // GS_TRAJECTORY_CALC_H