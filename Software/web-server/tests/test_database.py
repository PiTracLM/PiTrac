import sqlite3
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from db.database import Database


@pytest.mark.unit
class TestDatabase:
    @pytest.fixture
    def db(self, tmp_path):
        instance = Database(tmp_path / "test.db")
        yield instance
        instance.close()

    def test_migrations_apply_on_fresh_db(self, db):
        tables = {
            row["name"]
            for row in db.query("SELECT name FROM sqlite_master WHERE type='table'")
        }
        assert {"sessions", "shots", "shot_images"} <= tables

    def test_user_version_matches_latest_migration(self, db):
        version = db.query("PRAGMA user_version")[0][0]
        assert version == 1

    def test_reopening_is_idempotent(self, tmp_path):
        Database(tmp_path / "test.db").close()
        again = Database(tmp_path / "test.db")
        assert again.query("PRAGMA user_version")[0][0] == 1
        again.close()

    def test_wal_mode_enabled(self, db):
        assert db.query("PRAGMA journal_mode")[0][0] == "wal"

    def test_execute_returns_lastrowid(self, db):
        rowid = db.execute(
            "INSERT INTO sessions (started_at) VALUES (?)", ("2026-06-01T10:00:00",)
        )
        assert rowid == 1

    def test_crash_recovery(self, tmp_path):
        # First open — runs migrations, sets user_version=1
        db1 = Database(tmp_path / "test.db")
        db1.close()

        # Simulate a mid-migration crash by resetting user_version to 0
        conn = sqlite3.connect(tmp_path / "test.db")
        conn.execute("PRAGMA user_version = 0")
        conn.commit()
        conn.close()

        # Re-opening should re-run migrations without raising
        db2 = Database(tmp_path / "test.db")
        version = db2.query("PRAGMA user_version")[0][0]
        db2.close()

        assert version == 1

    def test_fk_enforcement(self, db):
        # Inserting a shot with a session_id that doesn't exist must fail
        with pytest.raises(sqlite3.IntegrityError):
            db.execute(
                "INSERT INTO shots (id, session_id, created_at, result_type) VALUES (?, ?, ?, ?)",
                (1699999999000, 999, "2026-06-01T10:00:00", "Normal"),
            )
