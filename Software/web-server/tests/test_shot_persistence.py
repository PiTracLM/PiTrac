import pytest
from unittest.mock import AsyncMock


def make_hit_payload(shot_id=1717236000123):
    return {
        "result_type": 7, "speed_mps": 65.0, "launch_angle": 12.4,
        "side_angle": -2.1, "back_spin": 2850, "side_spin": -310,
        "carry": 180, "message": "Ball Hit - Results returned.",
        "shot_id": shot_id,
        "images": ["shots/1717236000123/spin1.png", "shots/1717236000123/spin2.png"],
    }


@pytest.mark.unit
class TestShotPersistence:

    def test_hit_is_persisted(self, client, server_instance):
        resp = client.post("/api/internal/shot-result", json=make_hit_payload())
        assert resp.status_code == 200
        shot = server_instance.shot_repo.get(1717236000123)
        assert shot is not None
        assert shot["result_type"] == "Hit"
        assert len(shot["images"]) == 2

    def test_status_message_not_persisted(self, client, server_instance):
        client.post("/api/internal/shot-result", json={"result_type": 2, "message": "waiting"})
        assert server_instance.session_repo.list() == []

    def test_club_change_not_persisted(self, client, server_instance):
        client.post("/api/internal/shot-result",
                    json={"result_type": 10, "message": "Club type was set to Putter"})
        assert server_instance.session_repo.list() == []

    def test_duplicate_post_is_idempotent(self, client, server_instance):
        client.post("/api/internal/shot-result", json=make_hit_payload())
        client.post("/api/internal/shot-result", json=make_hit_payload())
        sessions = server_instance.session_repo.list()
        assert sessions[0]["shot_count"] == 1

    def test_hit_without_shot_id_still_persists(self, client, server_instance):
        payload = make_hit_payload()
        del payload["shot_id"]
        del payload["images"]
        client.post("/api/internal/shot-result", json=payload)
        assert server_instance.session_repo.list()[0]["shot_count"] == 1

    def test_stop_closes_open_session(self, client, server_instance):
        client.post("/api/internal/shot-result", json=make_hit_payload())
        assert server_instance.session_repo.list()[0]["ended_at"] is None

        server_instance.pitrac_manager.stop = AsyncMock(
            return_value={"status": "stopped"}
        )
        client.post("/api/pitrac/stop")

        assert server_instance.session_repo.list()[0]["ended_at"] is not None
