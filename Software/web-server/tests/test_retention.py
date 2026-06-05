import os
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from db.database import Database
from db.repositories import SessionRepository, ShotRepository
from models import ShotData
from retention import ImageRetention


# ── helpers ────────────────────────────────────────────────────────────────

def _hit(ts="2026-06-01T10:00:00"):
    return ShotData(
        speed=145.0, carry=265.0, launch_angle=12.0, side_angle=0.0,
        back_spin=2800, side_spin=0, result_type="Hit",
        message="Ball Hit", timestamp=ts,
    )


def _write_file(path: Path, size_bytes: int = 1024) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"x" * size_bytes)


def _make_session(sessions, shots, images_dir: Path, shot_id: int,
                  ts: str = "2026-06-01T10:00:00",
                  size_bytes: int = 512 * 1024) -> tuple[int, Path]:
    """Create a session with one shot + one image on disk. Returns (session_id, file_path)."""
    sid = sessions.ensure_open(ts, timeout_minutes=30)
    rel = f"shots/{shot_id}/spin1.png"
    disk_path = images_dir / rel
    _write_file(disk_path, size_bytes)
    shots.add(shot_id, sid, _hit(ts), [("spin1", rel)])
    return sid, disk_path


# ── fixtures ────────────────────────────────────────────────────────────────

@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "t.db")
    yield d
    d.close()


@pytest.fixture
def session_repo(db):
    return SessionRepository(db)


@pytest.fixture
def shot_repo(db):
    return ShotRepository(db)


@pytest.fixture
def images_dir(tmp_path):
    d = tmp_path / "images"
    d.mkdir()
    return d


def make_retention(session_repo, shot_repo, images_dir, cap_mb):
    return ImageRetention(session_repo, shot_repo, images_dir, cap_mb)


# ── tests ────────────────────────────────────────────────────────────────────

@pytest.mark.unit
class TestImageRetention:

    def test_under_cap_is_noop(self, session_repo, shot_repo, images_dir):
        sid, fpath = _make_session(session_repo, shot_repo, images_dir,
                                   shot_id=1, size_bytes=100)
        session_repo.close_open("2026-06-01T10:30:00")

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=500)
        r.prune()

        assert fpath.exists()
        assert len(shot_repo.image_paths_for_session(sid)) == 1

    def test_prunes_oldest_ended_session_first(self, session_repo, shot_repo, images_dir):
        # session 1 (older) — 600 KB
        sid1, f1 = _make_session(session_repo, shot_repo, images_dir,
                                  shot_id=1, ts="2026-06-01T09:00:00",
                                  size_bytes=600 * 1024)
        session_repo.close_open("2026-06-01T09:30:00")

        # session 2 (newer) — 100 KB (small, will survive once sid1 pruned)
        sid2, f2 = _make_session(session_repo, shot_repo, images_dir,
                                  shot_id=2, ts="2026-06-01T10:00:00",
                                  size_bytes=100 * 1024)
        session_repo.close_open("2026-06-01T10:30:00")

        # total ~700 KB; cap 200 KB → prune sid1 (600 KB); remainder 100 KB ≤ 200 KB → done
        r = ImageRetention(session_repo, shot_repo, images_dir, cap_mb=0)
        r._cap_bytes = 200 * 1024
        r.prune()

        assert not f1.exists(), "oldest session image should be pruned"
        assert f2.exists(), "newer session image should survive"
        assert shot_repo.image_paths_for_session(sid1) == []
        assert len(shot_repo.image_paths_for_session(sid2)) == 1
        # shot ROW for sid1 still present — rows are never deleted
        assert shot_repo.get(1) is not None

    def test_open_session_never_pruned(self, session_repo, shot_repo, images_dir):
        sid, fpath = _make_session(session_repo, shot_repo, images_dir,
                                   shot_id=10, size_bytes=600 * 1024)
        # session is still OPEN

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=0)
        r.prune()

        assert fpath.exists(), "open session image must survive"
        assert len(shot_repo.image_paths_for_session(sid)) == 1

    def test_terminates_when_only_unprunable_remains(self, session_repo, shot_repo, images_dir):
        # open session only; over cap → prune should return cleanly
        _make_session(session_repo, shot_repo, images_dir,
                      shot_id=20, size_bytes=600 * 1024)

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=0)
        # must not hang
        r.prune()
        # image still there
        assert (images_dir / "shots" / "20" / "spin1.png").exists()

    def test_orphan_dir_pruned_when_old_and_over_cap(self, session_repo, shot_repo, images_dir):
        # create an orphan dir — no shot_images row
        orphan_dir = images_dir / "shots" / "999"
        orphan_dir.mkdir(parents=True)
        orphan_file = orphan_dir / "frame.png"
        _write_file(orphan_file, 600 * 1024)

        # backdate mtime to 2 days ago
        old = time.time() - 48 * 3600
        os.utime(orphan_dir, (old, old))

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=0)
        r.prune()

        assert not orphan_dir.exists(), "old orphan dir should be deleted"

    def test_orphan_dir_kept_when_recent(self, session_repo, shot_repo, images_dir):
        orphan_dir = images_dir / "shots" / "888"
        orphan_dir.mkdir(parents=True)
        orphan_file = orphan_dir / "frame.png"
        _write_file(orphan_file, 600 * 1024)
        # mtime is current (fresh write) — should be kept

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=0)
        r.prune()

        assert orphan_dir.exists(), "recent orphan dir must be kept"

    def test_shot_rows_survive_pruning(self, session_repo, shot_repo, images_dir):
        sid, _ = _make_session(session_repo, shot_repo, images_dir,
                               shot_id=5, size_bytes=600 * 1024)
        session_repo.close_open("2026-06-01T10:30:00")

        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=0)
        r.prune()

        shot = shot_repo.get(5)
        assert shot is not None, "shot row must survive pruning"
        assert shot["result_type"] == "Hit"
        # images column is empty (rows deleted) but shot exists
        assert shot["images"] == []

    def test_empty_shots_dir_counts_as_zero(self, session_repo, shot_repo, images_dir):
        # no shots dir at all → noop, no crash
        r = make_retention(session_repo, shot_repo, images_dir, cap_mb=1)
        r.prune()  # should not raise


@pytest.mark.unit
class TestShotIdsWithImages:

    def test_returns_ids_that_have_image_rows(self, session_repo, shot_repo):
        sid = session_repo.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shot_repo.add(101, sid, _hit(), [("spin1", "shots/101/s.png")])
        shot_repo.add(102, sid, _hit(), [])  # no images
        result = shot_repo.shot_ids_with_images()
        assert result == {101}

    def test_empty_when_no_images(self, session_repo, shot_repo):
        sid = session_repo.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shot_repo.add(200, sid, _hit(), [])
        assert shot_repo.shot_ids_with_images() == set()

    def test_reflects_deletions(self, session_repo, shot_repo):
        sid = session_repo.ensure_open("2026-06-01T10:00:00", timeout_minutes=30)
        shot_repo.add(300, sid, _hit(), [("spin1", "shots/300/s.png")])
        session_repo.close_open("2026-06-01T10:30:00")
        shot_repo.delete_images_for_session(sid)
        assert shot_repo.shot_ids_with_images() == set()
