-- what a simulator reported back for a shot; distances in meters as the simulator sends them
CREATE TABLE IF NOT EXISTS shot_sim_results (
    shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
    simulator_id TEXT NOT NULL,
    carry REAL,
    total REAL,
    roll REAL,
    height REAL,
    lateral REAL,
    club_id TEXT,
    club_name TEXT,
    received_at TEXT NOT NULL,
    PRIMARY KEY (shot_id, simulator_id)
);
