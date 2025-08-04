package com.verdanttechs.jakarta.ee9;

import jakarta.servlet.ServletException;
import jakarta.servlet.annotation.WebServlet;
import jakarta.servlet.http.HttpServlet;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import java.io.IOException;
import java.util.Random;

/**
 * Mock data servlet to demonstrate carry distance calculation in PiTrac web interface
 * This simulates the data flow from ball detection through trajectory calculation to display
 */
@WebServlet("/mockdata")
public class MockDataServlet extends HttpServlet {
    
    private static Random random = new Random();
    
    @Override
    protected void doGet(HttpServletRequest request, HttpServletResponse response) 
            throws ServletException, IOException {
        
        // Simulate different shot types with realistic data
        MockShotData shotData = generateMockShot();
        
        // Set attributes for JSP display (matching MonitorServlet structure)
        request.setAttribute("speed_mph", shotData.speedMphStr);
        request.setAttribute("carry_yards", shotData.carryYardsStr);
        request.setAttribute("launch_angle_deg", shotData.launchAngleDegStr);
        request.setAttribute("side_angle_deg", shotData.sideAngleDegStr);
        request.setAttribute("back_spin_rpm", shotData.backSpinRpmStr);
        request.setAttribute("side_spin_rpm", shotData.sideSpinRpmStr);
        request.setAttribute("result_type", "Ball Hit - Mock Data");
        request.setAttribute("message", shotData.message);
        request.setAttribute("control_buttons", 
            "<button onclick='location.reload()'>Generate New Shot</button>");
        request.setAttribute("images", "");
        
        // Forward to the main dashboard JSP
        request.getRequestDispatcher("/WEB-INF/gs_dashboard.jsp").forward(request, response);
    }
    
    private static class MockShotData {
        String speedMphStr;
        String carryYardsStr;
        String launchAngleDegStr;
        String sideAngleDegStr;
        String backSpinRpmStr;
        String sideSpinRpmStr;
        String message;
    }
    
    private MockShotData generateMockShot() {
        MockShotData shot = new MockShotData();
        
        // Generate realistic shot parameters
        double speed = 120 + random.nextDouble() * 60; // 120-180 mph
        double vla = 8 + random.nextDouble() * 10;     // 8-18 degrees
        double hla = -5 + random.nextDouble() * 10;    // -5 to +5 degrees  
        int backspin = 2000 + random.nextInt(4000);    // 2000-6000 rpm
        int sidespin = -1000 + random.nextInt(2000);   // -1000 to +1000 rpm
        
        // Calculate carry using our trajectory physics (simplified version)
        double carryYards = calculateMockCarry(speed, vla, backspin);
        
        // Format for display
        shot.speedMphStr = String.format("%.1f mph", speed);
        shot.carryYardsStr = String.format("%.0f yards", carryYards);
        shot.launchAngleDegStr = String.format("%.1f°", vla);
        shot.sideAngleDegStr = String.format("%.1f°", hla);
        shot.backSpinRpmStr = String.format("%d rpm", backspin);
        
        String sideSpinDirection = sidespin >= 0 ? "L " : "R ";
        shot.sideSpinRpmStr = sideSpinDirection + String.format("%d rpm", Math.abs(sidespin));
        
        // Add descriptive message
        String shotType = getShotType(speed, carryYards);
        shot.message = String.format("🎯 %s - Calculated using PiTrac Trajectory Physics", shotType);
        
        return shot;
    }
    
    private double calculateMockCarry(double speedMph, double launchAngleDeg, int backspinRpm) {
        // Simplified trajectory calculation matching our gs_trajectory_calc logic
        double velocityMs = speedMph * 0.44704; // mph to m/s
        double launchAngleRad = Math.toRadians(launchAngleDeg);
        
        // Basic projectile motion with drag and spin effects
        double dragFactor = 0.95; // Simplified drag reduction
        double gravity = 9.81;
        
        // Time of flight
        double flightTime = 2.0 * velocityMs * Math.sin(launchAngleRad) / gravity * dragFactor;
        
        // Carry distance
        double carryMeters = velocityMs * Math.cos(launchAngleRad) * flightTime * dragFactor;
        double carryYards = carryMeters * 1.09361; // meters to yards
        
        // Apply spin effects
        double spinFactor = 1.0 + (backspinRpm / 10000.0) * 0.1;
        carryYards *= spinFactor;
        
        return carryYards;
    }
    
    private String getShotType(double speed, double carry) {
        if (speed > 160 && carry > 250) {
            return "Bomb Drive";
        } else if (speed > 140 && carry > 200) {
            return "Good Drive";
        } else if (speed < 100) {
            return "Short Iron";
        } else if (carry < 150) {
            return "Layup Shot";
        } else {
            return "Fairway Shot";
        }
    }
}