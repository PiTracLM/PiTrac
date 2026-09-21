import json
import uuid
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

    def delete_images_for_session(self, session_id: int) -> None:
        self.db.execute(
            "DELETE FROM shot_images WHERE shot_id IN (SELECT id FROM shots WHERE session_id = ?)",
            (session_id,),
        )

    def oldest_shot_with_images_excluding_latest(self) -> Optional[int]:
        rows = self.db.query(
            """SELECT MIN(shot_id) AS id FROM shot_images
               WHERE shot_id != (SELECT MAX(id) FROM shots)"""
        )
        return rows[0]["id"]

    def delete_images_for_shot(self, shot_id: int) -> None:
        self.db.execute("DELETE FROM shot_images WHERE shot_id = ?", (shot_id,))

    def shot_ids_with_images(self) -> set:
        """Set of shot ids that have at least one shot_images row."""
        rows = self.db.query("SELECT DISTINCT shot_id FROM shot_images")
        return {r["shot_id"] for r in rows}

    def shot_ids_for_session(self, session_id: int) -> list:
        rows = self.db.query("SELECT id FROM shots WHERE session_id = ?", (session_id,))
        return [r["id"] for r in rows]


class KeyValueRepository:
    def __init__(self, db: Database, table: str):
        if table not in ("settings", "calibration"):
            raise ValueError(f"Unknown key-value table: {table}")
        self.db = db
        self.table = table

    def load(self) -> Dict[str, Any]:
        rows = self.db.query(f"SELECT key, value FROM {self.table}")
        return {r["key"]: json.loads(r["value"]) for r in rows}

    def replace_all(self, flat: Dict[str, Any]) -> None:
        with self.db.transaction() as conn:
            self.replace_all_in(conn, flat)

    def updated_at(self, key: str) -> Optional[str]:
        rows = self.db.query(f"SELECT updated_at FROM {self.table} WHERE key = ?", (key,))
        return rows[0]["updated_at"] if rows else None

    def replace_all_in(self, conn, flat: Dict[str, Any]) -> None:
        # A row keeps its updated_at while its value is unchanged, so the timestamp says when that key last changed
        now = datetime.now().isoformat()
        old = {r["key"]: (r["value"], r["updated_at"]) for r in conn.execute(f"SELECT key, value, updated_at FROM {self.table}")}
        rows = []
        for key, value in flat.items():
            encoded = json.dumps(value)
            prev_value, prev_updated = old.get(key, (None, None))
            rows.append((key, encoded, prev_updated if prev_value == encoded else now))
        conn.execute(f"DELETE FROM {self.table}")
        conn.executemany(f"INSERT INTO {self.table} (key, value, updated_at) VALUES (?, ?, ?)", rows)


class SimulatorRepository:
    def __init__(self, db: Database):
        self.db = db

    @staticmethod
    def _row(row) -> Dict[str, Any]:
        return {
            "id": row["id"],
            "type": row["type"],
            "name": row["name"],
            "on": bool(row["enabled"]),
            "settings": json.loads(row["settings"]),
            "created_at": row["created_at"],
            "updated_at": row["updated_at"],
        }

    def list(self) -> List[Dict[str, Any]]:
        return [self._row(r) for r in self.db.query("SELECT * FROM simulators ORDER BY rowid")]

    def get(self, sim_id: str) -> Optional[Dict[str, Any]]:
        rows = self.db.query("SELECT * FROM simulators WHERE id = ?", (sim_id,))
        return self._row(rows[0]) if rows else None

    def add(self, sim_type: str, name: str, on: bool, settings: Dict[str, Any]) -> Dict[str, Any]:
        with self.db.transaction() as conn:
            sim_id = self.add_in(conn, sim_type, name, on, settings)
        return self.get(sim_id)

    def add_in(self, conn, sim_type: str, name: str, on: bool, settings: Dict[str, Any]) -> str:
        sim_id = uuid.uuid4().hex
        now = datetime.now().isoformat()
        conn.execute(
            """INSERT INTO simulators (id, type, name, enabled, settings, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (sim_id, sim_type, name, int(on), json.dumps(settings), now, now),
        )
        return sim_id

    def update(self, sim_id: str, name: str, on: bool, settings: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        self.db.execute(
            "UPDATE simulators SET name = ?, enabled = ?, settings = ?, updated_at = ? WHERE id = ?",
            (name, int(on), json.dumps(settings), datetime.now().isoformat(), sim_id),
        )
        return self.get(sim_id)

    def delete(self, sim_id: str) -> bool:
        with self.db.transaction() as conn:
            return conn.execute("DELETE FROM simulators WHERE id = ?", (sim_id,)).rowcount > 0
