#include "gs_trajectory_calc.h"
#include <iostream>
#include <cmath>
#include <algorithm>

// libshotscope integration - proper production approach
#include <libshotscope.hpp>

PiTracTrajectoryCalculator::PiTracTrajectoryCalculator() {
    // Constructor - future libshotscope initialization
}

PiTracTrajectoryCalculator::~PiTracTrajectoryCalculator() {
    // Destructor - future libshotscope cleanup
}

TrajectoryResult PiTracTrajectoryCalculator::calculateCarry(const TrajectoryInput& input) {
    TrajectoryResult result;
    
    // Validate input parameters
    if (!validateInput(input)) {
        result.calculation_successful = false;
        result.error_message = "Invalid input parameters";
        result.carry_distance_yards = 0.0;
        return result;
    }
    
    try {
        // Apply default atmospheric conditions for missing data
        TrajectoryInput complete_input = applyDefaults(input);
        
        // Convert PiTrac input to libshotscope format
        golfBall ball = {
            .x0 = 0.0f,  // Starting position
            .y0 = 0.0f,
            .z0 = 0.0f,
            .exitSpeed = static_cast<float>(complete_input.initial_velocity_mph),
            .launchAngle = static_cast<float>(complete_input.vertical_launch_angle_deg),
            .direction = static_cast<float>(complete_input.horizontal_launch_angle_deg),
            .backspin = static_cast<float>(complete_input.backspin_rpm),
            .sidespin = static_cast<float>(complete_input.sidespin_rpm)
        };
        
        atmosphericData atmos = {
            .temp = static_cast<float>(complete_input.temperature_f.value_or(DEFAULT_TEMPERATURE_F)),
            .elevation = static_cast<float>(complete_input.elevation_ft.value_or(DEFAULT_ELEVATION_FT)),
            .vWind = static_cast<float>(complete_input.wind_speed_mph.value_or(DEFAULT_WIND_SPEED_MPH)),
            .phiWind = static_cast<float>(complete_input.wind_direction_deg.value_or(DEFAULT_WIND_DIRECTION_DEG)),
            .hWind = 0.0f,  // Wind at ground level
            .relHumidity = static_cast<float>(complete_input.humidity_percent.value_or(DEFAULT_HUMIDITY_PERCENT)),
            .pressure = static_cast<float>(complete_input.pressure_inhg.value_or(DEFAULT_PRESSURE_INHG))
        };
        
        // Initialize libshotscope physics
        GolfBallPhysicsVariables physVars(ball, atmos);
        GolfBallFlight flight(physVars, ball, atmos);
        Simulator simulator(flight);
        
        // Get accurate landing position (stops at ground level)
        Vector3D landing = simulator.runSimulationLanding();
        
        // Get full trajectory for additional metrics (requires fresh simulator)
        GolfBallPhysicsVariables physVars2(ball, atmos);
        GolfBallFlight flight2(physVars2, ball, atmos);
        Simulator simulator2(flight2);
        std::vector<Vector3D> trajectory = simulator2.runSimulation();
        
        // Convert results
        result.carry_distance_yards = landing[1]; // Forward distance in yards
        result.calculation_successful = true;
        result.error_message = "Calculated using libshotscope physics (Prof. Alan Nathan research)";
        
        // Calculate additional metrics from trajectory
        if (!trajectory.empty()) {
            // Find flight time - when ball reaches landing position
            float landing_distance = landing[1];
            result.flight_time_seconds = trajectory.size() * 0.01f; // Default fallback
            
            // Find time when ball reaches the landing distance
            for (size_t i = 1; i < trajectory.size(); ++i) {
                if (trajectory[i][1] >= landing_distance) {
                    result.flight_time_seconds = i * 0.01f;
                    break;
                }
            }
            
            // Find maximum height
            float max_height = 0.0f;
            for (const auto& point : trajectory) {
                if (point[2] > max_height) {
                    max_height = point[2];
                }
            }
            result.max_height_yards = max_height;
            
            // Landing angle (approximate from last few trajectory points)
            if (trajectory.size() >= 2) {
                const auto& last = trajectory.back();
                const auto& prev = trajectory[trajectory.size() - 2];
                float dz = last[2] - prev[2];
                float dy = last[1] - prev[1];
                result.landing_angle_deg = std::atan2(dz, dy) * 180.0 / M_PI;
            }
        }
        
        return result;
        
    } catch (const std::exception& e) {
        result.calculation_successful = false;
        result.error_message = std::string("libshotscope calculation error: ") + e.what();
        result.carry_distance_yards = 0.0;
        return result;
    }
}

std::vector<std::array<double, 3>> PiTracTrajectoryCalculator::calculateFullTrajectory(const TrajectoryInput& input) {
    std::vector<std::array<double, 3>> trajectory;
    
    if (!validateInput(input)) {
        return trajectory; // Return empty trajectory for invalid input
    }
    
    try {
        // Apply default atmospheric conditions for missing data
        TrajectoryInput complete_input = applyDefaults(input);
        
        // Convert to libshotscope format (same as in calculateCarry)
        golfBall ball = {
            .x0 = 0.0f,
            .y0 = 0.0f,
            .z0 = 0.0f,
            .exitSpeed = static_cast<float>(complete_input.initial_velocity_mph),
            .launchAngle = static_cast<float>(complete_input.vertical_launch_angle_deg),
            .direction = static_cast<float>(complete_input.horizontal_launch_angle_deg),
            .backspin = static_cast<float>(complete_input.backspin_rpm),
            .sidespin = static_cast<float>(complete_input.sidespin_rpm)
        };
        
        atmosphericData atmos = {
            .temp = static_cast<float>(complete_input.temperature_f.value_or(DEFAULT_TEMPERATURE_F)),
            .elevation = static_cast<float>(complete_input.elevation_ft.value_or(DEFAULT_ELEVATION_FT)),
            .vWind = static_cast<float>(complete_input.wind_speed_mph.value_or(DEFAULT_WIND_SPEED_MPH)),
            .phiWind = static_cast<float>(complete_input.wind_direction_deg.value_or(DEFAULT_WIND_DIRECTION_DEG)),
            .hWind = 0.0f,
            .relHumidity = static_cast<float>(complete_input.humidity_percent.value_or(DEFAULT_HUMIDITY_PERCENT)),
            .pressure = static_cast<float>(complete_input.pressure_inhg.value_or(DEFAULT_PRESSURE_INHG))
        };
        
        // Initialize libshotscope physics
        GolfBallPhysicsVariables physVars(ball, atmos);
        GolfBallFlight flight(physVars, ball, atmos);
        Simulator simulator(flight);
        
        // Get full trajectory from libshotscope
        std::vector<Vector3D> libshotscope_trajectory = simulator.runSimulation();
        
        // Convert libshotscope Vector3D to PiTrac format
        trajectory.reserve(libshotscope_trajectory.size());
        for (const auto& point : libshotscope_trajectory) {
            trajectory.push_back({
                static_cast<double>(point[0]), // x (side deviation)
                static_cast<double>(point[1]), // y (forward distance)  
                static_cast<double>(point[2])  // z (height)
            });
        }
        
    } catch (const std::exception& e) {
        // Return empty trajectory on error
        trajectory.clear();
    }
    
    return trajectory;
}

bool PiTracTrajectoryCalculator::validateInput(const TrajectoryInput& input) {
    // Validate velocity
    if (input.initial_velocity_mph < MIN_VELOCITY_MPH || 
        input.initial_velocity_mph > MAX_VELOCITY_MPH) {
        return false;
    }
    
    // Validate launch angles
    if (input.vertical_launch_angle_deg < MIN_LAUNCH_ANGLE_DEG || 
        input.vertical_launch_angle_deg > MAX_LAUNCH_ANGLE_DEG) {
        return false;
    }
    
    if (abs(input.horizontal_launch_angle_deg) > 45.0) {
        return false;
    }
    
    // Validate spin rates
    if (abs(input.backspin_rpm) > MAX_SPIN_RPM || 
        abs(input.sidespin_rpm) > MAX_SPIN_RPM) {
        return false;
    }
    
    return true;
}

std::pair<golfBall, atmosphericData> PiTracTrajectoryCalculator::convertToLibshotscopeFormat(const TrajectoryInput& input) {
    TrajectoryInput complete_input = applyDefaults(input);
    
    golfBall ball = {
        .x0 = 0.0f,
        .y0 = 0.0f,
        .z0 = 0.0f,
        .exitSpeed = static_cast<float>(complete_input.initial_velocity_mph),
        .launchAngle = static_cast<float>(complete_input.vertical_launch_angle_deg),
        .direction = static_cast<float>(complete_input.horizontal_launch_angle_deg),
        .backspin = static_cast<float>(complete_input.backspin_rpm),
        .sidespin = static_cast<float>(complete_input.sidespin_rpm)
    };
    
    atmosphericData atmos = {
        .temp = static_cast<float>(complete_input.temperature_f.value_or(DEFAULT_TEMPERATURE_F)),
        .elevation = static_cast<float>(complete_input.elevation_ft.value_or(DEFAULT_ELEVATION_FT)),
        .vWind = static_cast<float>(complete_input.wind_speed_mph.value_or(DEFAULT_WIND_SPEED_MPH)),
        .phiWind = static_cast<float>(complete_input.wind_direction_deg.value_or(DEFAULT_WIND_DIRECTION_DEG)),
        .hWind = 0.0f,
        .relHumidity = static_cast<float>(complete_input.humidity_percent.value_or(DEFAULT_HUMIDITY_PERCENT)),
        .pressure = static_cast<float>(complete_input.pressure_inhg.value_or(DEFAULT_PRESSURE_INHG))
    };
    
    return std::make_pair(ball, atmos);
}

TrajectoryInput PiTracTrajectoryCalculator::applyDefaults(const TrajectoryInput& input) {
    TrajectoryInput complete_input = input;
    
    // Apply default atmospheric conditions if not provided
    if (!complete_input.temperature_f.has_value()) {
        complete_input.temperature_f = DEFAULT_TEMPERATURE_F;
    }
    
    if (!complete_input.elevation_ft.has_value()) {
        complete_input.elevation_ft = DEFAULT_ELEVATION_FT;
    }
    
    if (!complete_input.wind_speed_mph.has_value()) {
        complete_input.wind_speed_mph = DEFAULT_WIND_SPEED_MPH;
    }
    
    if (!complete_input.wind_direction_deg.has_value()) {
        complete_input.wind_direction_deg = DEFAULT_WIND_DIRECTION_DEG;
    }
    
    if (!complete_input.humidity_percent.has_value()) {
        complete_input.humidity_percent = DEFAULT_HUMIDITY_PERCENT;
    }
    
    if (!complete_input.pressure_inhg.has_value()) {
        complete_input.pressure_inhg = DEFAULT_PRESSURE_INHG;
    }
    
    return complete_input;
}