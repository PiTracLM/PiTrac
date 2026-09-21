from __future__ import annotations

import asyncio
import functools
import inspect
import logging
from typing import Any, Awaitable, Callable, Dict, List, Optional, Tuple, Type

from config_manager import HOST_ERROR, _is_valid_host, coerce
from db.repositories import KeyValueRepository, SimulatorRepository
from models import ShotData
from sim_interface import STATUS_CONNECTED, STATUS_ERROR, STATUS_OFF, SimInterface
from sims.e6_sim import E6Sim
from sims.gspro_sim import GSProSim
from sims.ogs_sim import OGSSim

logger = logging.getLogger(__name__)

SIM_TYPES: Dict[str, Type[SimInterface]] = {cls.type: cls for cls in (GSProSim, E6Sim, OGSSim)}

# The C++ sent a ball-detected heartbeat when it found the ball, just before the stabilization status
_BALL_DETECTED_BY_STATUS = {
    "Waiting For Ball": False,
    "Waiting For Placement To Stabilize": True,
    "Ball Placed": True,
}

BroadcastFn = Callable[[Dict[str, object]], Awaitable[None]]


class SimSettingsError(ValueError):
    def __init__(self, fields: Dict[str, str]):
        self.fields = fields
        super().__init__("; ".join(fields.values()))


def validate_settings(cls: Type[SimInterface], settings: Dict[str, Any]) -> Tuple[Dict[str, Any], Dict[str, str]]:
    """The type's fields from settings with defaults filled in, and an error per field that does not validate"""
    clean: Dict[str, Any] = {}
    errors: Dict[str, str] = {}
    for field in cls.FIELDS:
        key, label = field["key"], field["label"]
        value = settings.get(key, field.get("default"))
        if field["type"] == "host":
            value = "" if value is None else str(value).strip()
            if not value and field.get("required"):
                errors[key] = f"{label} is required"
            elif not _is_valid_host(value):
                errors[key] = HOST_ERROR
        else:
            try:
                value = coerce("integer", value)
            except ValueError:
                errors[key] = f"{label} must be a whole number"
                continue
            if not field["min"] <= value <= field["max"]:
                errors[key] = f"{label} must be between {field['min']} and {field['max']}"
        if key not in errors:
            clean[key] = value
    return clean, errors


def _sim_type(name: Any) -> Type[SimInterface]:
    cls = SIM_TYPES.get(name)
    if cls is None:
        raise SimSettingsError({"type": f"Unknown simulator type: {name}"})
    return cls


def _valid_settings(cls: Type[SimInterface], settings: Any) -> Dict[str, Any]:
    if not isinstance(settings, dict):
        raise SimSettingsError({"settings": "Settings must be an object"})
    clean, errors = validate_settings(cls, settings)
    if errors:
        raise SimSettingsError(errors)
    return clean


def _object(body: Any) -> Dict[str, Any]:
    if not isinstance(body, dict):
        raise SimSettingsError({"body": "Send a JSON object"})
    return body


def _on(value: Any) -> bool:
    try:
        return coerce("boolean", value)
    except ValueError:
        raise SimSettingsError({"on": "On must be true or false"}) from None


def import_legacy_settings(settings: KeyValueRepository, sims: SimulatorRepository) -> bool:
    """Turn the old one-per-type simulators.<type>.* settings into instances and remove them, in one write"""
    stored = settings.load()
    old = {key: value for key, value in stored.items() if key.startswith("simulators.")}
    if not old:
        return False
    existing = {(s["type"], s["settings"].get("host"), s["settings"].get("port")) for s in sims.list()}
    with settings.db.transaction() as conn:
        for cls in SIM_TYPES.values():
            prefix = f"simulators.{cls.type}."
            values = {key[len(prefix):]: value for key, value in old.items() if key.startswith(prefix)}
            try:
                on = coerce("boolean", values.get("enabled", False))
            except ValueError:
                on = False
            host = values.get("host")
            if not on and not (isinstance(host, str) and host.strip()):
                continue
            clean, errors = validate_settings(cls, values)
            for key, error in errors.items():
                logger.warning(f"Old {cls.display_name} setting {key}={values.get(key)!r} not kept: {error}")
            fields = {field["key"]: field.get("default") for field in cls.FIELDS} | clean
            if (cls.type, fields.get("host"), fields.get("port")) in existing:
                logger.info(f"Old {cls.display_name} settings match a saved simulator, not adding another")
                continue
            sims.add_in(conn, cls.type, cls.display_name, on, fields)
            logger.info(f"Moved the old {cls.display_name} settings into a simulator named {cls.display_name}")
        settings.replace_all_in(conn, {key: value for key, value in stored.items() if key not in old})
    return True


class SimManager:
    RELOAD_DEBOUNCE_SEC = 0.5
    TEST_TIMEOUT_SEC = 5

    def __init__(self, repo: SimulatorRepository, broadcast: Optional[BroadcastFn] = None) -> None:
        self.repo = repo
        self._broadcast = broadcast
        self._sims: Dict[str, SimInterface] = {}
        self._build_errors: Dict[str, str] = {}
        self._running = False
        self._reload_tasks: Dict[str, asyncio.Task] = {}
        self._connect_tasks: Dict[str, asyncio.Task] = {}
        self._reload_lock = asyncio.Lock()
        self._ball_detected: Optional[bool] = None
        self._club: Optional[str] = None
        self._club_sim: Optional[str] = None

    # -- Instances --

    def types(self) -> List[Dict[str, Any]]:
        return [{"type": cls.type, "display_name": cls.display_name, "fields": cls.FIELDS} for cls in SIM_TYPES.values()]

    def _default_name(self, cls: Type[SimInterface], exclude_id: Optional[str] = None) -> str:
        taken = {inst["name"] for inst in self.repo.list() if inst["id"] != exclude_id}
        name, n = cls.display_name, 1
        while name in taken:
            n += 1
            name = f"{cls.display_name} {n}"
        return name

    def create(self, body: Any) -> Dict[str, Any]:
        body = _object(body)
        cls = _sim_type(body.get("type"))
        settings = _valid_settings(cls, body.get("settings", {}))
        name = str(body.get("name") or "").strip() or self._default_name(cls)
        inst = self.repo.add(cls.type, name, _on(body.get("on", True)), settings)
        self._schedule_reload(inst["id"])
        return self._payload(inst)

    def update(self, sim_id: str, body: Any) -> Dict[str, Any]:
        body = _object(body)
        inst = self.repo.get(sim_id)
        if inst is None:
            raise KeyError(sim_id)
        cls = _sim_type(inst["type"])
        if body.get("type", inst["type"]) != inst["type"]:
            raise SimSettingsError({"type": "The type of a saved simulator can't change"})
        settings = body.get("settings", {})
        if isinstance(settings, dict):
            settings = {**inst["settings"], **settings}
        settings = _valid_settings(cls, settings)
        name = inst["name"]
        if "name" in body:
            name = str(body["name"] or "").strip() or self._default_name(cls, exclude_id=sim_id)
        on = _on(body["on"]) if "on" in body else inst["on"]
        inst = self.repo.update(sim_id, name, on, settings)
        self._schedule_reload(sim_id)
        return self._payload(inst)

    def delete(self, sim_id: str) -> None:
        if not self.repo.delete(sim_id):
            raise KeyError(sim_id)
        self._schedule_reload(sim_id)

    def _payload(self, inst: Dict[str, Any]) -> Dict[str, Any]:
        cls = SIM_TYPES.get(inst["type"])
        sim = self._sims.get(inst["id"])
        if sim is not None:
            live = sim.info()
        elif inst["id"] in self._build_errors:
            live = {"status": STATUS_ERROR, "detail": self._build_errors[inst["id"]]}
        else:
            live = {"status": STATUS_OFF, "detail": ""}
        host, port = inst["settings"].get("host"), inst["settings"].get("port")
        return {
            "id": inst["id"],
            "type": inst["type"],
            "name": inst["name"],
            "display_type": cls.display_name if cls else inst["type"],
            "on": inst["on"],
            "status": live["status"],
            "target": f"{host}:{port}" if host else "",
            "detail": live["detail"],
            "settings": inst["settings"],
        }

    def status(self) -> List[Dict[str, Any]]:
        return [self._payload(inst) for inst in self.repo.list()]

    # -- Running sims --

    def _build(self, inst: Dict[str, Any]) -> None:
        try:
            cls = _sim_type(inst["type"])
            settings = _valid_settings(cls, inst["settings"])
            hooks = {
                "on_club": functools.partial(self.set_club, inst["id"]),
                "ball_state": lambda: bool(self._ball_detected),
            }
            accepted = inspect.signature(cls).parameters
            sim = cls(**settings, **{key: hook for key, hook in hooks.items() if key in accepted})
        except (TypeError, ValueError, OverflowError) as e:
            logger.error(f"Simulator {inst['name']} settings are invalid, not started: {e}")
            self._build_errors[inst["id"]] = f"Invalid settings: {e}"
            return
        sim.set_status_callback(functools.partial(self._on_sim_status, inst["id"], sim))
        self._sims[inst["id"]] = sim

    def build_sims(self) -> None:
        self._sims = {}
        self._build_errors = {}
        for inst in self.repo.list():
            if inst["on"]:
                self._build(inst)

    def _connect_soon(self, sim_id: str) -> None:
        self._connect_tasks[sim_id] = asyncio.create_task(self._sims[sim_id].connect())

    async def start(self) -> None:
        self._running = True
        self.build_sims()
        for sim_id in self._sims:
            self._connect_soon(sim_id)
        await self._broadcast_status()

    async def stop(self) -> None:
        self._running = False
        for task in self._reload_tasks.values():
            task.cancel()
        self._reload_tasks = {}
        for sim_id in list(self._sims):
            await self._stop_sim(sim_id)

    def _schedule_reload(self, sim_id: str) -> None:
        if not self._running:
            return
        if sim_id in self._reload_tasks:
            self._reload_tasks[sim_id].cancel()
        self._reload_tasks[sim_id] = asyncio.create_task(self._reload_after_debounce(sim_id))

    async def _reload_after_debounce(self, sim_id: str) -> None:
        await asyncio.sleep(self.RELOAD_DEBOUNCE_SEC)
        async with self._reload_lock:
            await self._stop_sim(sim_id)
            self._sims.pop(sim_id, None)
            self._build_errors.pop(sim_id, None)
            inst = self.repo.get(sim_id)
            if inst is not None and inst["on"]:
                self._build(inst)
                if sim_id in self._sims:
                    self._connect_soon(sim_id)
        await self._broadcast_status()

    async def _stop_sim(self, sim_id: str) -> None:
        task = self._connect_tasks.pop(sim_id, None)
        if task is not None:
            task.cancel()
        sim = self._sims.get(sim_id)
        if sim is None:
            return
        try:
            await sim.disconnect()
        except Exception as e:
            logger.warning(f"sim {sim_id} disconnect failed: {e}")

    async def on_shot(self, shot: ShotData) -> None:
        # A reload can drop or add a sim while a send is awaited
        for sim_id, sim in list(self._sims.items()):
            if sim.status != STATUS_CONNECTED:
                continue
            try:
                await sim.send_shot(shot)
            except Exception as e:
                logger.warning(f"sim {sim_id} send_shot failed: {e}")

    async def on_status(self, result_type: str) -> None:
        ball_detected = _BALL_DETECTED_BY_STATUS.get(result_type)
        if ball_detected is None or ball_detected == self._ball_detected:
            return
        self._ball_detected = ball_detected
        # A reload can drop or add a sim while a send is awaited
        for sim_id, sim in list(self._sims.items()):
            if sim.status != STATUS_CONNECTED:
                continue
            try:
                await sim.on_ball_state(ball_detected)
            except Exception as e:
                logger.warning(f"sim {sim_id} on_ball_state failed: {e}")

    @property
    def armed(self) -> bool:
        return all(
            sim.armed for sim in self._sims.values() if sim.status == STATUS_CONNECTED and hasattr(sim, "armed")
        )

    @property
    def club(self) -> Optional[str]:
        return self._club

    def set_club(self, sim_id: str, club: str) -> None:
        self._club = club
        self._club_sim = sim_id

    async def connect(self, sim_id: str) -> None:
        await (await self._current_sim(sim_id)).connect()

    async def disconnect(self, sim_id: str) -> None:
        await (await self._current_sim(sim_id)).disconnect()

    async def _current_sim(self, sim_id: str) -> SimInterface:
        # Waits out a reload in progress, so the request reaches the rebuilt sim and not one being torn down
        async with self._reload_lock:
            sim = self._sims.get(sim_id)
        if sim is None:
            raise KeyError(sim_id)
        return sim

    async def test_connection(self, body: Any) -> Dict[str, Any]:
        """Connect a throwaway sim, handshake included, and disconnect it; never sends a shot"""
        body = _object(body)
        cls = _sim_type(body.get("type"))
        settings = _valid_settings(cls, body.get("settings", {}))
        where = f"{cls.display_name} at {settings['host']}:{settings['port']}"
        sim = cls(**settings)
        settled = asyncio.Event()

        async def on_status() -> None:
            if sim.status in (STATUS_CONNECTED, STATUS_ERROR):
                settled.set()

        sim.set_status_callback(on_status)
        try:
            await asyncio.wait_for(asyncio.gather(sim.connect(), settled.wait()), self.TEST_TIMEOUT_SEC)
            ok, detail = sim.status == STATUS_CONNECTED, sim.info()["detail"]
        except asyncio.TimeoutError:
            ok, detail = False, f"no answer within {self.TEST_TIMEOUT_SEC} seconds"
        finally:
            await sim.disconnect()
        if ok:
            return {"ok": True, "message": f"Connected to {where}"}
        return {"ok": False, "message": f"Could not connect to {where}: {detail}"}

    async def _on_sim_status(self, sim_id: str, sim: SimInterface) -> None:
        if sim_id == self._club_sim and sim.status != STATUS_CONNECTED:
            self._club = None
            self._club_sim = None
        await self._broadcast_status()

    async def _broadcast_status(self) -> None:
        if self._broadcast is None:
            return
        try:
            await self._broadcast({"type": "sim_status", "sims": self.status()})
        except Exception as e:
            logger.warning(f"sim status broadcast failed: {e}")
