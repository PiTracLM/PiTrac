from datetime import datetime, timedelta
from typing import Any, Dict, List, Optional, Tuple

from db.database import Database
from models import ShotData


class SessionRepository:
    def __init__(self, db: Database):
        self.db = db

    def ensure_open(self, now_iso: str, timeout_minutes: int) -> int:
        rows = self.db.query(
            "SELECT id, started_at FROM sessions WHERE ended_at IS NULL ORDER BY id DESC LIMIT 1"
        )
        if rows:
            session_id = rows[0]["id"]
            last = self.db.query(
                "SELECT MAX(created_at) AS t FROM shots WHERE session_id = ?", (session_id,)
            )[0]["t"] or rows[0]["started_at"]
            cutoff = datetime.fromisoformat(now_iso) - timedelta(minutes=timeout_minutes)
            if datetime.fromisoformat(last) >= cutoff:
                return session_id
            self.db.execute("UPDATE sessions SET ended_at = ? WHERE id = ?", (now_iso, session_id))
        return self.db.execute("INSERT INTO sessions (started_at) VALUES (?)", (now_iso,))

    def close_open(self, now_iso: str) -> None:
        self.db.execute("UPDATE sessions SET ended_at = ? WHERE ended_at IS NULL", (now_iso,))

    def get(self, session_id: int) -> Optional[Dict[str, Any]]:
        rows = self.db.query("SELECT * FROM sessions WHERE id = ?", (session_id,))
        return dict(rows[0]) if rows else None

    def list(self, limit: int = 50, offset: int = 0) -> List[Dict[str, Any]]:
        rows = self.db.query(
            """SELECT s.*, COUNT(sh.id) AS shot_count
               FROM sessions s LEFT JOIN shots sh ON sh.session_id = s.id
               GROUP BY s.id ORDER BY s.id DESC LIMIT ? OFFSET ?""",
            (limit, offset),
        )
        return [dict(r) for r in rows]

    def oldest_prunable(self) -> Optional[Dict[str, Any]]:
        # oldest ended session that still has image rows — keeps the prune loop advancing
        rows = self.db.query(
            """SELECT * FROM sessions WHERE ended_at IS NOT NULL
               AND EXISTS (SELECT 1 FROM shot_images si JOIN shots s ON s.id = si.shot_id
                           WHERE s.session_id = sessions.id)
               ORDER BY id ASC LIMIT 1"""
        )
        return dict(rows[0]) if rows else None

    def delete(self, session_id: int) -> None:
        self.db.execute("DELETE FROM sessions WHERE id = ?", (session_id,))


class ShotRepository:
    def __init__(self, db: Database):
        self.db = db

    def add(self, shot_id: int, session_id: int, shot: ShotData,
            images: List[Tuple[str, str]]) -> None:
        self.db.execute(
            """INSERT OR IGNORE INTO shots
               (id, session_id, created_at, result_type, speed, carry,
                launch_angle, side_angle, back_spin, side_spin, message)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (shot_id, session_id, shot.timestamp, shot.result_type, shot.speed,
             shot.carry, shot.launch_angle, shot.side_angle, shot.back_spin,
             shot.side_spin, shot.message),
        )
        # first writer wins: a re-post with different images keeps the originals,
        # but a shot that arrived imageless can pick them up later
        existing = self.db.query("SELECT COUNT(*) FROM shot_images WHERE shot_id = ?", (shot_id,))
        if existing[0][0] == 0 and images:
            self.db.executemany(
                "INSERT INTO shot_images (shot_id, kind, file_path) VALUES (?, ?, ?)",
                [(shot_id, kind, path) for kind, path in images],
            )

    def _attach_images(self, shot_row: Dict[str, Any]) -> Dict[str, Any]:
        imgs = self.db.query(
            "SELECT kind, file_path FROM shot_images WHERE shot_id = ? ORDER BY id",
            (shot_row["id"],),
        )
        shot_row["images"] = [dict(i) for i in imgs]
        return shot_row

    def get(self, shot_id: int) -> Optional[Dict[str, Any]]:
        rows = self.db.query("SELECT * FROM shots WHERE id = ?", (shot_id,))
        return self._attach_images(dict(rows[0])) if rows else None

    def list_for_session(self, session_id: int) -> List[Dict[str, Any]]:
        rows = self.db.query(
            "SELECT * FROM shots WHERE session_id = ? ORDER BY id DESC", (session_id,)
        )
        return [self._attach_images(dict(r)) for r in rows]

    def image_paths_for_session(self, session_id: int) -> List[str]:
        rows = self.db.query(
            """SELECT si.file_path FROM shot_images si
               JOIN shots s ON s.id = si.shot_id WHERE s.session_id = ?""",
            (session_id,),
        )
        return [r["file_path"] for r in rows]

    def delete_images_for_session(self, session_id: int) -> None:
        self.db.execute(
            "DELETE FROM shot_images WHERE shot_id IN (SELECT id FROM shots WHERE session_id = ?)",
            (session_id,),
        )
