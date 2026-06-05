import logging
import shutil
from datetime import datetime, timedelta
from pathlib import Path

logger = logging.getLogger(__name__)

# Orphan dirs younger than this are left alone — they may still be receiving writes.
_ORPHAN_GRACE = timedelta(hours=24)


def _dir_bytes(shots_dir: Path) -> int:
    """Sum file sizes under shots_dir; returns 0 if the dir doesn't exist."""
    if not shots_dir.is_dir():
        return 0
    return sum(f.stat().st_size for f in shots_dir.rglob("*") if f.is_file())


class ImageRetention:
    def __init__(self, session_repo, shot_repo, images_dir: Path, cap_mb: int):
        self._sessions = session_repo
        self._shots = shot_repo
        self._images_dir = images_dir
        self._cap_bytes = cap_mb * 1024 * 1024

    def prune(self, now: datetime | None = None) -> None:
        shots_dir = self._images_dir / "shots"
        total = _dir_bytes(shots_dir)
        if total <= self._cap_bytes:
            return

        # Step 2: prune ended sessions oldest-first until under cap or no prunable remain.
        # oldest_prunable() advances each iteration because we remove that session's image
        # rows, so it can never return the same session twice → no infinite loop.
        while total > self._cap_bytes:
            session = self._sessions.oldest_prunable()
            if session is None:
                break  # only open sessions or image-less sessions remain; stop
            sid = session["id"]
            for rel in self._shots.image_paths_for_session(sid):
                (self._images_dir / rel).unlink(missing_ok=True)
            # Remove emptied per-shot dirs
            self._rmdir_if_empty_for_session(sid)
            self._shots.delete_images_for_session(sid)
            total = _dir_bytes(shots_dir)
            logger.info(f"pruned session {sid} images; remaining {total // (1024*1024)}MB")

        # Step 3: orphan sweep — shots/<id> dirs with no shot_images rows.
        # C++ may write images for error-status shots that never get persisted.
        if total > self._cap_bytes and shots_dir.is_dir():
            _now = now or datetime.now()
            cutoff = (_now - _ORPHAN_GRACE).timestamp()
            known = self._shots.shot_ids_with_images()
            for d in shots_dir.iterdir():
                if not d.is_dir():
                    continue
                try:
                    dir_id = int(d.name)
                except ValueError:
                    continue
                if dir_id in known:
                    continue
                if d.stat().st_mtime <= cutoff:
                    shutil.rmtree(d, ignore_errors=True)
                    logger.info(f"pruned orphan dir {d.name}")

    def _rmdir_if_empty_for_session(self, session_id: int) -> None:
        """Remove shots/<shot_id> dirs that are now empty for a given session's shots."""
        shot_ids = self._shots.shot_ids_for_session(session_id)
        shots_dir = self._images_dir / "shots"
        for sid in shot_ids:
            d = shots_dir / str(sid)
            if d.is_dir():
                try:
                    d.rmdir()  # only removes if empty
                except OSError:
                    pass  # non-empty or already gone; leave it
