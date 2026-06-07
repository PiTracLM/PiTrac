"""
Tests for the log_files helper module.
"""

import pytest
from datetime import datetime
from pathlib import Path

from log_files import (
    run_log_path,
    list_run_logs,
    latest_run_log,
    tail_lines,
    read_chunk_before,
    prune_run_logs,
    truncate_if_over,
    BLOCK,
)


class TestRunLogPath:
    @pytest.mark.unit
    def test_name_format(self, tmp_path):
        result = run_log_path(tmp_path, datetime(2026, 6, 12, 18, 30, 45))
        assert result.name == "pitrac-20260612-183045.log"
        assert result.parent == tmp_path


class TestListRunLogs:
    @pytest.mark.unit
    def test_returns_only_strict_pattern_files(self, tmp_path):
        # create matching run logs
        (tmp_path / "pitrac-20260610-100000.log").write_text("a")
        (tmp_path / "pitrac-20260611-120000.log").write_text("b")
        (tmp_path / "pitrac-20260612-183045.log").write_text("c")
        # stray files that must NOT appear
        (tmp_path / "pitrac.log").write_text("legacy")
        (tmp_path / "other.log").write_text("noise")
        (tmp_path / "pitrac-bad.log").write_text("no timestamp")

        results = list_run_logs(tmp_path)
        names = [p.name for p in results]

        assert names == [
            "pitrac-20260610-100000.log",
            "pitrac-20260611-120000.log",
            "pitrac-20260612-183045.log",
        ]

    @pytest.mark.unit
    def test_empty_dir(self, tmp_path):
        assert list_run_logs(tmp_path) == []

    @pytest.mark.unit
    def test_nonexistent_dir(self, tmp_path):
        assert list_run_logs(tmp_path / "nope") == []


class TestLatestRunLog:
    @pytest.mark.unit
    def test_returns_newest_run_file_not_legacy(self, tmp_path):
        # pitrac.log sorts AFTER "pitrac-2026..." lexically ("pitrac." > "pitrac-")
        # but latest_run_log must never return it
        (tmp_path / "pitrac-20260610-100000.log").write_text("old run")
        (tmp_path / "pitrac-20260612-183045.log").write_text("new run")
        (tmp_path / "pitrac.log").write_text("legacy — must not be returned")

        result = latest_run_log(tmp_path)
        assert result is not None
        assert result.name == "pitrac-20260612-183045.log"

    @pytest.mark.unit
    def test_returns_none_when_no_run_logs(self, tmp_path):
        # only legacy present
        (tmp_path / "pitrac.log").write_text("legacy")
        assert latest_run_log(tmp_path) is None

    @pytest.mark.unit
    def test_returns_none_for_empty_dir(self, tmp_path):
        assert latest_run_log(tmp_path) is None


class TestTailLines:
    @pytest.mark.unit
    def test_small_file_fewer_lines_than_n(self, tmp_path):
        path = tmp_path / "small.log"
        path.write_text("alpha\nbeta\ngamma\n")
        lines, offset = tail_lines(path, 100)
        assert lines == ["alpha", "beta", "gamma"]
        assert offset == 0

    @pytest.mark.unit
    def test_trailing_newline_offset(self, tmp_path):
        path = tmp_path / "trail.log"
        content = "line1\nline2\nline3\nline4\nline5\n"
        path.write_text(content)
        lines, offset = tail_lines(path, 3)
        assert lines == ["line3", "line4", "line5"]
        # offset should be at the start of "line3\n"
        assert offset == len("line1\nline2\n")

    @pytest.mark.unit
    def test_no_trailing_newline_offset(self, tmp_path):
        path = tmp_path / "notail.log"
        content = "line1\nline2\nline3\nline4\nline5"  # no final newline
        path.write_bytes(content.encode())
        lines, offset = tail_lines(path, 3)
        assert lines == ["line3", "line4", "line5"]
        assert offset == len("line1\nline2\n")

    @pytest.mark.unit
    def test_large_file_block_boundary(self, tmp_path):
        """5MB file with numbered lines — tail must return the exact last 100."""
        path = tmp_path / "big.log"
        line_template = "line {}\n"
        total = 0
        lines_written = 0
        with open(path, "wb") as f:
            i = 0
            while total < 5 * 1024 * 1024:
                data = f"line {i}\n".encode()
                f.write(data)
                total += len(data)
                lines_written = i + 1
                i += 1

        n = 100
        result_lines, _ = tail_lines(path, n)
        assert len(result_lines) == n
        # the last written line index is lines_written - 1
        expected = [f"line {lines_written - n + j}" for j in range(n)]
        assert result_lines == expected

    @pytest.mark.unit
    def test_exact_n_lines(self, tmp_path):
        path = tmp_path / "exact.log"
        path.write_text("\n".join(f"L{i}" for i in range(10)) + "\n")
        lines, offset = tail_lines(path, 10)
        assert lines == [f"L{i}" for i in range(10)]
        assert offset == 0


class TestReadChunkBefore:
    @pytest.mark.unit
    def test_reads_lines_before_tail_offset(self, tmp_path):
        path = tmp_path / "chunk.log"
        content = "\n".join(f"line{i}" for i in range(20)) + "\n"
        path.write_text(content)

        tail, tail_offset = tail_lines(path, 5)
        assert len(tail) == 5

        prev_lines, prev_offset = read_chunk_before(path, tail_offset, 5)
        assert len(prev_lines) == 5
        assert prev_lines[-1] == "line14"  # line just before the tail window
        assert prev_offset < tail_offset

    @pytest.mark.unit
    def test_offset_zero_returns_empty(self, tmp_path):
        path = tmp_path / "zero.log"
        path.write_text("some content\n")
        lines, offset = read_chunk_before(path, 0, 10)
        assert lines == []
        assert offset == 0


class TestPruneRunLogs:
    @pytest.mark.unit
    def test_deletes_oldest_run_files_first(self, tmp_path):
        old = tmp_path / "pitrac-20260601-000000.log"
        mid = tmp_path / "pitrac-20260602-000000.log"
        new = tmp_path / "pitrac-20260603-000000.log"
        for p in (old, mid, new):
            p.write_bytes(b"x" * 1000)

        # cap = 2000 → need to drop at least one; old should go first
        prune_run_logs(tmp_path, 2000)

        assert not old.exists()
        assert new.exists()  # latest never deleted

    @pytest.mark.unit
    def test_legacy_pitrac_log_deleted_before_run_files(self, tmp_path):
        legacy = tmp_path / "pitrac.log"
        run1 = tmp_path / "pitrac-20260601-000000.log"
        run2 = tmp_path / "pitrac-20260602-000000.log"
        legacy.write_bytes(b"x" * 2000)
        run1.write_bytes(b"x" * 500)
        run2.write_bytes(b"x" * 500)  # latest run file

        # cap = 2000; total = 3000 → need to shed 1000; legacy is first candidate
        prune_run_logs(tmp_path, 2000)

        assert not legacy.exists()
        assert run1.exists()
        assert run2.exists()  # latest always kept

    @pytest.mark.unit
    def test_under_cap_nothing_deleted(self, tmp_path):
        run1 = tmp_path / "pitrac-20260601-000000.log"
        run2 = tmp_path / "pitrac-20260602-000000.log"
        run1.write_bytes(b"x" * 100)
        run2.write_bytes(b"x" * 100)

        prune_run_logs(tmp_path, 1_000_000)

        assert run1.exists()
        assert run2.exists()

    @pytest.mark.unit
    def test_single_latest_file_never_deleted_even_if_over_cap(self, tmp_path):
        sole = tmp_path / "pitrac-20260601-000000.log"
        sole.write_bytes(b"x" * 10000)

        prune_run_logs(tmp_path, 1)  # cap of 1 byte, but sole is the latest

        assert sole.exists()


class TestTruncateIfOver:
    @pytest.mark.unit
    def test_truncates_oversized_file(self, tmp_path):
        path = tmp_path / "big.log"
        path.write_bytes(b"x" * 1000)
        result = truncate_if_over(path, 500)
        assert result is True
        assert path.stat().st_size == 0

    @pytest.mark.unit
    def test_leaves_undersized_file_alone(self, tmp_path):
        path = tmp_path / "small.log"
        path.write_bytes(b"x" * 100)
        result = truncate_if_over(path, 500)
        assert result is False
        assert path.stat().st_size == 100

    @pytest.mark.unit
    def test_nonexistent_file_returns_false(self, tmp_path):
        result = truncate_if_over(tmp_path / "nope.log", 500)
        assert result is False
