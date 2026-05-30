CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at TEXT NOT NULL,
    ended_at TEXT,
    label TEXT
);

-- shot ids come from the C++ process (epoch ms at detection), not autoincrement
CREATE TABLE IF NOT EXISTS shots (
    id INTEGER PRIMARY KEY,
    session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    result_type TEXT NOT NULL,
    speed REAL NOT NULL DEFAULT 0,
    carry REAL NOT NULL DEFAULT 0,
    launch_angle REAL NOT NULL DEFAULT 0,
    side_angle REAL NOT NULL DEFAULT 0,
    back_spin INTEGER NOT NULL DEFAULT 0,
    side_spin INTEGER NOT NULL DEFAULT 0,
    message TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_shots_session ON shots(session_id);

CREATE TABLE IF NOT EXISTS shot_images (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    file_path TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_shot_images_shot ON shot_images(shot_id);
