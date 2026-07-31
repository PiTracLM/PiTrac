from unittest.mock import MagicMock, patch

import pytest


def detection(count):
    return {
        "success": True,
        "cameras": [{"index": i} for i in range(count)],
        "configuration": {"slot1": {"type": 4, "lens": 1}, "slot2": {"type": 4, "lens": 1}},
    }


@pytest.mark.unit
class TestCameraAutodetect:
    async def test_saves_detected_slot_on_empty_settings(self, server_instance):
        with patch("server.CameraDetector") as detector:
            detector.return_value.detect.return_value = detection(1)
            detector.return_value.detect.return_value["configuration"]["slot1"]["lens"] = 2
            await server_instance._detect_cameras_if_unset()

        saved = server_instance.config_manager._settings.load()
        assert saved["cameras.slot1.type"] == "4"
        assert saved["cameras.slot1.lens"] == "2"
        assert not any(key.startswith("cameras.slot2") for key in saved)

    async def test_existing_db_never_runs_detection(self, server_instance):
        server_instance.db.created = False
        with patch("server.CameraDetector") as detector:
            await server_instance._detect_cameras_if_unset()

        detector.assert_not_called()

    async def test_legacy_settings_file_skips_detection(self, monkeypatch, tmp_path):
        import server as server_module

        monkeypatch.setenv("HOME", str(tmp_path))
        monkeypatch.setattr(server_module, "DB_PATH", tmp_path / "test.db")
        legacy = tmp_path / ".pitrac" / "config" / "user_settings.json"
        legacy.parent.mkdir(parents=True)
        legacy.write_text('{"cameras": {"slot1": {"type": "5"}}}')
        srv = server_module.PiTracServer()
        with patch("server.CameraDetector") as detector:
            await srv._detect_cameras_if_unset()

        detector.assert_not_called()

    async def test_detection_failure_leaves_settings_empty(self, server_instance):
        with patch("server.CameraDetector", MagicMock(side_effect=RuntimeError("no libcamera"))):
            await server_instance._detect_cameras_if_unset()

        assert "cameras.slot1.type" not in server_instance.config_manager._settings.load()

    async def test_unsupported_detected_type_is_not_saved(self, server_instance):
        with patch("server.CameraDetector") as detector:
            detector.return_value.detect.return_value = detection(1)
            detector.return_value.detect.return_value["configuration"]["slot1"].update(type=0, lens=2)
            await server_instance._detect_cameras_if_unset()

        saved = server_instance.config_manager._settings.load()
        assert "cameras.slot1.type" not in saved
        assert saved["cameras.slot1.lens"] == "2"
