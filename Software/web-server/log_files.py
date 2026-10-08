import asyncio
import os
import re
from datetime import datetime
from pathlib import Path
from typing import List, Optional, Tuple

RUN_LOG_RE = re.compile(r"^pitrac-\d{8}-\d{6}\.log$")
BLOCK = 8192


def run_log_path(log_dir: Path, started_at: datetime) -> Path:
    return log_dir / f"pitrac-{started_at:%Y%m%d-%H%M%S}.log"


def list_run_logs(log_dir: Path) -> List[Path]:
    if not log_dir.is_dir():
        return []
    return sorted(p for p in log_dir.glob("pitrac-*.log") if RUN_LOG_RE.match(p.name))


def latest_run_log(log_dir: Path) -> Optional[Path]:
    logs = list_run_logs(log_dir)
    return logs[-1] if logs else None


def _read_back(path: Path, end: int, n: int) -> Tuple[bytes, int]:
    """Read backwards from byte `end` until >= n newlines collected. Returns (data, start)."""
    chunks, newlines = [], 0
    with open(path, "rb") as f:
        while end > 0 and newlines <= n:
            start = max(0, end - BLOCK)
            f.seek(start)
            chunk = f.read(end - start)
            chunks.append(chunk)
            newlines += chunk.count(b"\n")
            end = start
    return b"".join(reversed(chunks)), end


def _last_lines(data: bytes, data_start: int, data_end: int, n: int) -> Tuple[List[str], int]:
    if not data:
        return [], data_start
    trailing_newline = data.endswith(b"\n")
    parts = data.split(b"\n")
    if trailing_newline:
        parts.pop()
    selected = parts[-n:]
    selected_bytes = sum(len(p) + 1 for p in selected)
    if not trailing_newline and selected:
        selected_bytes -= 1
    offset = data_end - selected_bytes
    return [p.decode("utf-8", errors="replace") for p in selected], max(data_start, offset)


def tail_lines(path: Path, n: int) -> Tuple[List[str], int]:
    size = path.stat().st_size
    data, start = _read_back(path, size, n)
    return _last_lines(data, start, size, n)


def read_chunk_before(path: Path, offset: int, n: int) -> Tuple[List[str], int]:
    """n lines ending at byte offset; returns (lines, new_offset). offset 0 → ([], 0)."""
    end = min(offset, path.stat().st_size)
    data, start = _read_back(path, end, n)
    return _last_lines(data, start, end, n)


def prune_run_logs(log_dir: Path, cap_bytes: int) -> None:
    # legacy single-file log from old installs is always the first prune candidate
    logs = [p for p in [log_dir / "pitrac.log"] if p.exists()] + list_run_logs(log_dir)
    total = sum(p.stat().st_size for p in logs)
    for p in logs[:-1]:  # never the live/latest file
        if total <= cap_bytes:
            break
        total -= p.stat().st_size
        p.unlink(missing_ok=True)


def truncate_if_over(path: Path, cap_bytes: int) -> bool:
    if path.exists() and path.stat().st_size > cap_bytes:
        os.truncate(path, 0)
        return True
    return False


async def follow(path: Path, poll_interval: float = 0.5):
    """Yield new lines appended to `path`, polling for growth. Resets on truncate."""
    pos = path.stat().st_size if path.exists() else 0
    pending = b""
    while True:
        if path.exists():
            size = path.stat().st_size
            if size < pos:        # truncate backstop fired (marathon-run cap)
                pos = 0
                pending = b""
            if size > pos:
                with open(path, "rb") as f:
                    f.seek(pos)
                    pending += f.read(size - pos)
                    pos = size
                # hold back any trailing partial line until its newline arrives
                *complete, pending = pending.split(b"\n")
                for line in complete:
                    yield line.decode("utf-8", errors="replace")
        await asyncio.sleep(poll_interval)
