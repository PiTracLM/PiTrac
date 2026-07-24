import pytest
from fastapi.testclient import TestClient


def make_hit_payload(shot_id=1717236000123):
    return {
        "result_type": 7,
        "speed_mps": 65.0,
        "launch_angle": 12.4,
        "side_angle": -2.1,
        "back_spin": 2850,
        "side_spin": -310,
        "carry": 180,
        "message": "Ball Hit - Results returned.",
        "shot_id": shot_id,
        "images": [
            f"shots/{shot_id}/spin1.png",
            f"shots/{shot_id}/spin2.png",
        ],
    }


@pytest.fixture
def history_server(monkeypatch, tmp_path):
    """Server with both DB and IMAGES_DIR redirected to tmp_path."""
    import server as server_module
    monkeypatch.setattr(server_module, "DB_PATH", tmp_path / "test.db")
    monkeypatch.setattr(server_module, "IMAGES_DIR", tmp_path / "images")
    from server import PiTracServer
    srv = PiTracServer()
    return srv


@pytest.fixture
def history_client(history_server):
    return TestClient(history_server.app)


@pytest.mark.unit
class TestSessionsEndpoint:

    def test_list_sessions_with_shots(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(111))
        history_client.post("/api/internal/shot-result", json=make_hit_payload(222))

        resp = history_client.get("/api/sessions")
        assert resp.status_code == 200
        sessions = resp.json()
        assert len(sessions) == 1
        assert sessions[0]["shot_count"] == 2

    def test_list_sessions_empty(self, history_client):
        resp = history_client.get("/api/sessions")
        assert resp.status_code == 200
        assert resp.json() == []

    def test_list_sessions_limit_offset(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(333))
        resp = history_client.get("/api/sessions?limit=5&offset=0")
        assert resp.status_code == 200
        assert isinstance(resp.json(), list)

    def test_list_sessions_newest_first(self, history_client, history_server):
        # Force two separate sessions by closing the first one
        history_client.post("/api/internal/shot-result", json=make_hit_payload(444))
        history_server.session_repo.close_open("2020-01-01T00:00:00")
        history_client.post("/api/internal/shot-result", json=make_hit_payload(555))

        sessions = history_client.get("/api/sessions").json()
        assert len(sessions) == 2
        assert sessions[0]["id"] > sessions[1]["id"]


@pytest.mark.unit
class TestShotsForSessionEndpoint:

    def test_shots_for_session_returns_list_with_images(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(1001))
        session_id = history_server.session_repo.list()[0]["id"]

        resp = history_client.get(f"/api/sessions/{session_id}/shots")
        assert resp.status_code == 200
        shots = resp.json()
        assert len(shots) == 1
        assert shots[0]["result_type"] == "Hit"
        assert len(shots[0]["images"]) == 2
        assert shots[0]["images"][0]["file_path"].startswith("shots/")

    def test_shots_for_session_unknown_id_returns_404(self, history_client):
        resp = history_client.get("/api/sessions/99999/shots")
        assert resp.status_code == 404

    def test_shots_for_session_newest_first(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(2001))
        history_client.post("/api/internal/shot-result", json=make_hit_payload(2002))
        session_id = history_server.session_repo.list()[0]["id"]

        shots = history_client.get(f"/api/sessions/{session_id}/shots").json()
        assert len(shots) == 2
        assert shots[0]["id"] > shots[1]["id"]


@pytest.mark.unit
class TestGetShotEndpoint:

    def test_get_shot_returns_shot(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(3001))

        resp = history_client.get("/api/shots/3001")
        assert resp.status_code == 200
        shot = resp.json()
        assert shot["id"] == 3001
        assert shot["result_type"] == "Hit"
        assert "images" in shot

    def test_get_shot_unknown_id_returns_404(self, history_client):
        resp = history_client.get("/api/shots/99999")
        assert resp.status_code == 404


@pytest.mark.unit
class TestDeleteSessionEndpoint:

    def test_delete_removes_session_and_shots(self, history_client, history_server):
        history_client.post("/api/internal/shot-result", json=make_hit_payload(4001))
        session_id = history_server.session_repo.list()[0]["id"]

        resp = history_client.delete(f"/api/sessions/{session_id}")
        assert resp.status_code == 200
        body = resp.json()
        assert body["status"] == "deleted"
        assert body["session_id"] == session_id

        # DB rows gone
        assert history_server.session_repo.get(session_id) is None
        assert history_server.shot_repo.list_for_session(session_id) == []

    def test_delete_removes_image_files(self, history_client, history_server, tmp_path):
        shot_id = 4002
        shots_dir = (tmp_path / "images" / "shots" / str(shot_id))
        shots_dir.mkdir(parents=True)
        img1 = shots_dir / "spin1.png"
        img2 = shots_dir / "spin2.png"
        img1.write_bytes(b"fake")
        img2.write_bytes(b"fake")

        history_client.post("/api/internal/shot-result", json=make_hit_payload(shot_id))
        session_id = history_server.session_repo.list()[0]["id"]

        history_client.delete(f"/api/sessions/{session_id}")

        assert not img1.exists()
        assert not img2.exists()
        # parent dir also removed since it's now empty
        assert not shots_dir.exists()

    def test_delete_removes_shot_dir_by_id_not_stored_path(self, history_client, history_server, tmp_path):
        shot_id = 4003
        shot_dir = tmp_path / "images" / "shots" / str(shot_id)
        shot_dir.mkdir(parents=True)
        (shot_dir / "spin1.png").write_bytes(b"fake")
        (shot_dir / "extra.bin").write_bytes(b"fake")
        outside = tmp_path / "outside.png"
        outside.write_bytes(b"keep")
        payload = make_hit_payload(shot_id)
        payload["images"] = [f"shots/{shot_id}/spin1.png", "../outside.png"]

        history_client.post("/api/internal/shot-result", json=payload)
        session_id = history_server.session_repo.list()[0]["id"]
        history_client.delete(f"/api/sessions/{session_id}")

        assert not shot_dir.exists()
        assert outside.exists()

    def test_delete_unknown_session_returns_404(self, history_client):
        resp = history_client.delete("/api/sessions/99999")
        assert resp.status_code == 404


@pytest.mark.unit
class TestStorageUsageEndpoint:

    def test_storage_usage_returns_int_and_cap(self, history_client, history_server, tmp_path):
        resp = history_client.get("/api/storage/usage")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data["used_mb"], int)
        assert data["cap_mb"] == history_server.image_cap_mb

    def test_storage_usage_accounts_for_files(self, history_client, history_server, tmp_path):
        shots_dir = tmp_path / "images" / "shots" / "5001"
        shots_dir.mkdir(parents=True)
        # write 2 MB of data
        (shots_dir / "big.bin").write_bytes(b"x" * (2 * 1024 * 1024))

        resp = history_client.get("/api/storage/usage")
        assert resp.json()["used_mb"] >= 2


@pytest.mark.unit
class TestHistoryPageSmoke:

    def test_history_page_returns_200(self, history_client):
        resp = history_client.get("/history")
        assert resp.status_code == 200
        assert "text/html" in resp.headers["content-type"]
        assert "Shot History" in resp.text
        assert "history.js" in resp.text
