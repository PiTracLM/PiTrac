package com.verdanttechs.jakarta.ee9.type;

public enum GsIPCResultType {
    // NOTE - these should reflect types in gs_ipc_result
    kUnknown,
    kInitializing,
    kWaitingForBallToAppear,
    kWaitingForSimulatorArmed,
    kPausingForBallStabilization,
    kMultipleBallsPresent,
    kBallPlacedAndReadyForHit,
    kHit,
    kError,
    kCalibrationResults;
}
