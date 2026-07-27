import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from db.database import Database
from db.repositories import KeyValueRepository, SessionRepository, ShotRepository
from models import ShotData


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "test.db")
    yield d
    d.close()


@pytest.fixture
def sessions(db):
    return SessionRepository(db)


@pytest.fixture
def shots(db):
    return ShotRepository(db)


def hit(ts="2026-06-01T10:00:00"):
    return ShotData(speed=145.2, carry=265.0, launch_angle=12.4, side_angle=-2.1,
                    back_spin=2850, side_spin=-310, result_type="Hit",
                    message="Ball Hit", timestamp=ts)


@pytest.mark.unit
class TestSessionRepository:
    def test_ensure_session_creates_then_reuses(self, sessions):
        a = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        b = sessions.ensure_open("2026-06-01T10:10:00", timeout_minutes=30)
        assert a == b

    def test_ensure_session_rolls_over_after_timeout(self, sessions, shots, db):
        a = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1000, a, hit("2026-06-01T10:05:00"), [])
        b = sessions.ensure_open("2026-06-01T11:00:00", timeout_minutes=30)
        assert b != a
        assert sessions.get(a)["ended_at"] is not None

    def test_close_open_sets_ended_at(self, sessions):
        a = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        sessions.close_open("2026-06-01T10:30:00")
        assert sessions.get(a)["ended_at"] == "2026-06-01T10:30:00"

    def test_session_list_includes_shot_count(self, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1, s, hit(), [])
        shots.add(2, s, hit(), [])
        listed = sessions.list()
        assert listed[0]["shot_count"] == 2

    def test_delete_session_cascades(self, db, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1, s, hit(), [("spin1", "x.png")])
        sessions.delete(s)
        assert db.query("SELECT COUNT(*) FROM shots")[0][0] == 0
        assert db.query("SELECT COUNT(*) FROM shot_images")[0][0] == 0

    def test_oldest_prunable_returns_ended_session_with_images(self, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1, s, hit(), [("spin1", "x.png")])
        sessions.close_open("2026-06-01T10:30:00")
        result = sessions.oldest_prunable()
        assert result is not None
        assert result["id"] == s

    def test_oldest_prunable_ignores_open_sessions(self, sessions, shots):
        # open session with images — must not be returned
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1, s, hit(), [("spin1", "x.png")])
        result = sessions.oldest_prunable()
        assert result is None

    def test_oldest_prunable_ignores_ended_sessions_without_images(self, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1, s, hit(), [])  # no images
        sessions.close_open("2026-06-01T10:30:00")
        result = sessions.oldest_prunable()
        assert result is None

    def test_oldest_prunable_returns_oldest_when_multiple(self, sessions, shots):
        s1 = sessions.ensure_open("2026-06-01T09:00:00", timeout_minutes=30)
        shots.add(1, s1, hit(), [("spin1", "a.png")])
        sessions.close_open("2026-06-01T09:30:00")

        s2 = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(2, s2, hit(), [("spin1", "b.png")])
        sessions.close_open("2026-06-01T10:30:00")

        result = sessions.oldest_prunable()
        assert result["id"] == s1


@pytest.mark.unit
class TestShotRepository:
    def test_shot_insert_is_idempotent(self, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1717236000123, s, hit(), [("spin1", "shots/1717236000123/spin1.png")])
        shots.add(1717236000123, s, hit(), [("spin1", "shots/1717236000123/spin1.png")])
        rows = shots.list_for_session(s)
        assert len(rows) == 1
        assert len(rows[0]["images"]) == 1

    def test_get_returns_shot_with_images(self, sessions, shots):
        s = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(1717236000123, s, hit(), [("spin1", "a.png")])
        shot = shots.get(1717236000123)
        assert shot["result_type"] == "Hit"
        assert shot["images"] == [{"kind": "spin1", "file_path": "a.png"}]
        assert shots.get(999) is None

    def test_delete_images_for_session_removes_only_that_session(self, sessions, shots, db):
        s1 = sessions.ensure_open("2026-06-01T09:00:00", timeout_minutes=30)
        shots.add(1, s1, hit(), [("spin1", "s1.png")])
        sessions.close_open("2026-06-01T09:30:00")

        s2 = sessions.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shots.add(2, s2, hit(), [("spin1", "s2.png")])

        shots.delete_images_for_session(s1)

        remaining = db.query("SELECT file_path FROM shot_images")
        assert len(remaining) == 1
        assert remaining[0]["file_path"] == "s2.png"


@pytest.mark.unit
class TestKeyValueRepository:
    def test_replace_all_round_trips_values_and_drops_old_rows(self, db):
        repo = KeyValueRepository(db, "calibration")
        repo.replace_all({"stale.key": 1})
        flat = {"gs_config.cameras.kCamera1Angles": [2.14, -26.42], "gs_config.cameras.kCamera1FocalLength": 5.9}
        repo.replace_all(flat)
        assert repo.load() == flat

    def test_rejects_unknown_table(self, db):
        with pytest.raises(ValueError):
            KeyValueRepository(db, "shots")
