#!/usr/bin/env python3
"""
Test script for ball ready status integration.
This script simulates the various ball status messages that would be sent
from the C++ PiTrac system to verify the web interface updates correctly.
"""

import asyncio
import msgpack
import stomp
import time
from typing import Dict, Any

class GsIPCResultType:
    kUnknown = 0
    kInitializing = 1
    kWaitingForBallToAppear = 2
    kWaitingForSimulatorArmed = 3
    kPausingForBallStabilization = 4
    kMultipleBallsPresent = 5
    kBallPlacedAndReadyForHit = 6
    kHit = 7
    kError = 8
    kCalibrationResults = 9
    kControlMessage = 10


def send_test_message(conn: stomp.Connection, result_type: int, message: str) -> None:
    """Send a test status message to ActiveMQ."""
    
    test_data = [
        0,        # carry_meters
        0.0,      # speed_mpers
        0.0,      # launch_angle_deg
        0.0,      # side_angle_deg
        0,        # back_spin_rpm
        0,        # side_spin_rpm
        0,        # confidence
        0,        # club_type
        result_type,  # result_type
        message,  # message
        [],       # log_messages
        []        # image_file_paths
    ]
    
    packed = msgpack.packb(test_data)
    
    conn.send(body=packed, destination='/queue/golf_sim')
    print(f"Sent: {message} (type={result_type})")


def run_test_sequence():
    """Run through the full ball ready lifecycle."""
    
    print("Starting ball status integration test...")
    
    conn = stomp.Connection([('localhost', 61613)])
    
    try:
        conn.connect('admin', 'admin', wait=True)
        print("Connected to ActiveMQ")
        
        test_sequence = [
            (GsIPCResultType.kInitializing, "System starting up..."),
            (GsIPCResultType.kWaitingForSimulatorArmed, "Waiting for simulator to be ready"),
            (GsIPCResultType.kWaitingForBallToAppear, "Waiting for ball to be teed up"),
            (GsIPCResultType.kPausingForBallStabilization, "Ball detected, waiting for stability"),
            (GsIPCResultType.kBallPlacedAndReadyForHit, "Ball ready - Let's Golf!"),
            (GsIPCResultType.kHit, "Ball hit - processing results..."),
        ]
        
        print("\nSending test messages:")
        print("-" * 50)
        
        for result_type, message in test_sequence:
            send_test_message(conn, result_type, message)
            time.sleep(2)
        
        print("\nTesting error state:")
        send_test_message(conn, GsIPCResultType.kError, "Test error message")
        time.sleep(2)
        
        print("\nTesting multiple balls state:")
        send_test_message(conn, GsIPCResultType.kMultipleBallsPresent, "Multiple balls detected - please remove extra balls")
        time.sleep(2)
        
        print("\nReturning to ready state:")
        send_test_message(conn, GsIPCResultType.kBallPlacedAndReadyForHit, "Ball ready again!")
        
        print("\nTest sequence complete!")
        print("Check the web interface at http://localhost:8000 to verify the UI updates")
        
    except Exception as e:
        print(f"Error during test: {e}")
    finally:
        conn.disconnect()
        print("Disconnected from ActiveMQ")


if __name__ == "__main__":
    run_test_sequence()