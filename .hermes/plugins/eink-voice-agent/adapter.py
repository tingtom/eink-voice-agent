import asyncio
import json
import logging
import os
import subprocess
from datetime import datetime, timezone
from typing import Any, Dict
from pathlib import Path
from aiohttp import web

from gateway.platforms.base import (
    BasePlatformAdapter,
    MessageEvent,
    MessageType,
    SendResult,
)
from gateway.config import Platform, PlatformConfig

logger = logging.getLogger(__name__)

NOTES_DIR = Path.home() / ".eink-voice-agent" / "notes"
TODOS_FILE = Path.home() / ".eink-voice-agent" / "todos.json"
ACTIVITY_LOG = Path.home() / ".eink-voice-agent" / "activity.jsonl"
TELEMETRY_FILE = Path.home() / ".eink-voice-agent" / "telemetry.json"


def _ensure_dirs():
    NOTES_DIR.mkdir(parents=True, exist_ok=True)
    ACTIVITY_LOG.parent.mkdir(parents=True, exist_ok=True)


def _log_activity(entry: dict):
    entry["ts"] = datetime.now(timezone.utc).isoformat()
    try:
        with open(ACTIVITY_LOG, "a") as f:
            f.write(json.dumps(entry) + "\n")
        lines = ACTIVITY_LOG.read_text().splitlines()
        if len(lines) > 2000:
            ACTIVITY_LOG.write_text("\n".join(lines[-1500:]) + "\n")
    except Exception as e:
        logger.warning("Failed to write activity log: %s", e)


def _save_telemetry(data: dict):
    TELEMETRY_FILE.parent.mkdir(parents=True, exist_ok=True)
    data["updated_at"] = datetime.now(timezone.utc).isoformat()
    TELEMETRY_FILE.write_text(json.dumps(data, indent=2))


def _load_todos():
    if TODOS_FILE.exists():
        return json.loads(TODOS_FILE.read_text())
    return []


def _save_todos(todos):
    TODOS_FILE.parent.mkdir(parents=True, exist_ok=True)
    TODOS_FILE.write_text(json.dumps(todos, indent=2))


def _transcribe_audio(audio_base64: str) -> str:
    try:
        with open("/tmp/eink_audio_raw.bin", "wb") as f:
            f.write(__import__("base64").b64decode(audio_base64))

        result = subprocess.run(
            ["ffmpeg", "-y", "-f", "s16le", "-ar", "16000", "-ac", "1",
             "-i", "/tmp/eink_audio_raw.bin",
             "/tmp/eink_audio.wav"],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            logger.warning("ffmpeg failed: %s", result.stderr)
            return "[transcription failed]"

        result = subprocess.run(
            ["whisper", "/tmp/eink_audio.wav", "--language", "en",
             "--output_dir", "/tmp/", "--no_timestamps"],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode != 0:
            logger.warning("whisper failed: %s", result.stderr)
            return "[transcription failed]"

        txt_path = Path("/tmp/eink_audio.wav.txt")
        if txt_path.exists():
            return txt_path.read_text().strip()
        return "[empty transcription]"
    except Exception as e:
        logger.error("Transcription error: %s", e)
        return f"[transcription error: {e}]"


class EInkDeviceAdapter(BasePlatformAdapter):
    def __init__(self, config: PlatformConfig):
        super().__init__(config, Platform("eink_voice_agent"))
        self._port = int(os.getenv("EINK_DEVICE_PORT", "8123"))
        self._host = os.getenv("EINK_DEVICE_HOST", "0.0.0.0")
        self._http_server = None
        self._audio_buffers: dict[str, list[str]] = {}
        self._ws_by_chat: dict[str, object] = {}
        self._device_chat_map: dict[str, str] = {}
        _ensure_dirs()

    async def connect(self) -> bool:
        # Advertise via mDNS so ESP32 can discover us
        self._zeroconf = None
        try:
            from zeroconf import Zeroconf, ServiceInfo
            import socket
            host_ip = socket.gethostbyname(socket.gethostname())
            self._zeroconf = Zeroconf()
            self._service_info = ServiceInfo(
                "_eink-voice-gateway._tcp.local.",
                f"EInkVoiceGateway._eink-voice-gateway._tcp.local.",
                addresses=[socket.inet_aton(host_ip)],
                port=self._port,
                properties={"path": "/api/device/ws"},
            )
            self._zeroconf.register_service(self._service_info)
            logger.info("mDNS advertised _eink-voice-gateway._tcp on %s:%s", host_ip, self._port)
        except ImportError:
            logger.warning("pip install zeroconf for mDNS auto-discovery")
        except Exception as e:
            logger.warning("mDNS registration failed: %s", e)

        app = web.Application()
        app.add_routes([
            web.post("/api/device/auth", self._handle_auth),
            web.post("/api/device/audio", self._handle_audio),
            web.post("/api/device/audio/end", self._handle_audio_end),
            web.post("/api/device/telemetry", self._handle_telemetry),
        ])
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, self._host, self._port)
        await site.start()
        self._http_server = runner
        logger.info("E-Ink device HTTP server on http://%s:%s", self._host, self._port)
        self._mark_connected()
        return True

    async def disconnect(self) -> None:
        if self._zeroconf:
            try:
                self._zeroconf.unregister_service(self._service_info)
                self._zeroconf.close()
            except Exception:
                pass
        if self._http_server:
            await self._http_server.cleanup()
        self._mark_disconnected()

    async def _handle_auth(self, request):
        try:
            body = await request.json()
        except Exception:
            return web.json_response({"error": "invalid json"}, status=400)

        device_id = body.get("device_id", "unknown")
        chat_id = f"eink:{device_id}"
        self._device_chat_map[device_id] = chat_id
        logger.info("Device '%s' authenticated via HTTP", device_id)
        _log_activity({"type": "connect", "detail": f"Device '{device_id}' connected"})
        return web.json_response({"type": "auth_ok", "device_id": device_id})

    async def _handle_audio(self, request):
        device_id = request.headers.get("X-Device-ID", "unknown")
        chat_id = self._device_chat_map.get(device_id)
        if not chat_id:
            return web.json_response({"error": "not authenticated"}, status=401)

        session = request.query.get("session_id", chat_id)
        mode = request.query.get("mode", "agent")
        body = await request.read()

        import base64
        chunk_b64 = base64.b64encode(body).decode("ascii")
        if session not in self._audio_buffers:
            self._audio_buffers[session] = []
        self._audio_buffers[session].append(chunk_b64)
        return web.json_response({"status": "ok"})

    async def _handle_audio_end(self, request):
        device_id = request.headers.get("X-Device-ID", "unknown")
        chat_id = self._device_chat_map.get(device_id)
        if not chat_id:
            return web.json_response({"error": "not authenticated"}, status=401)

        try:
            body = await request.json()
        except Exception:
            return web.json_response({"error": "invalid json"}, status=400)

        session = body.get("session_id", chat_id)
        mode = body.get("mode", "agent")
        chunks = self._audio_buffers.pop(session, [])
        full_audio = "".join(chunks)

        try:
            reply = await self._process_end(session, mode, full_audio, chat_id, device_id)
            return web.json_response({"type": "response", "data": reply})
        except Exception as e:
            logger.error("Audio processing error: %s", e)
            return web.json_response({"error": str(e)}, status=500)

    async def _handle_telemetry(self, request):
        try:
            body = await request.json()
            telemetry = {k: v for k, v in body.items() if k not in ("type",)}
            _save_telemetry(telemetry)
            _log_activity({
                "type": "telemetry",
                "detail": "battery={}% wifi={}dBm".format(
                    telemetry.get("battery", "?"), telemetry.get("wifi_rssi", "?")
                ),
            })
            return web.json_response({"status": "ok"})
        except Exception as e:
            logger.error("Telemetry error: %s", e)
            return web.json_response({"error": str(e)}, status=400)

    async def _process_end(self, session, mode, full_audio, chat_id, device_id):
        if mode == "transcribe":
            text = _transcribe_audio(full_audio)
            timestamp = __import__("datetime").datetime.now().strftime(
                "%Y-%m-%d_%H-%M-%S"
            )
            note_file = NOTES_DIR / f"note_{timestamp}.txt"
            note_file.write_text(text)
            _log_activity({"type": "message", "direction": "in",
                           "content": f"[voice note] {text[:500]}".strip(),
                           "session_id": session})
            return f"Transcribed note saved:\n\n{text[:180]}"

        elif mode == "todo":
            if full_audio:
                text = _transcribe_audio(full_audio)
            else:
                text = "list my todos"
            source = self.build_source(
                chat_id=chat_id, chat_name=f"Device-{device_id}",
                chat_type="dm", user_id=device_id, user_name=device_id,
            )
            event = MessageEvent(
                text=text,
                message_type=MessageType.TEXT,
                source=source,
                message_id=session,
            )
            await self.handle_message(event)
            # Return a simple acknowledgment
            return "Todo command processed"

        else:
            source = self.build_source(
                chat_id=chat_id, chat_name=f"Device-{device_id}",
                chat_type="dm", user_id=device_id, user_name=device_id,
            )
            event = MessageEvent(
                text="[agent audio input]",
                message_type=MessageType.TEXT,
                source=source,
                message_id=session,
                extra_data={"audio_data": full_audio},
            )
            await self.handle_message(event)
            return "Audio received"

    async def send(self, chat_id, content, reply_to=None, metadata=None):
        # For HTTP mode, we don't push to device.
        # Device polls for messages or we return response inline.
        return SendResult(success=True, message_id="")

    async def get_chat_info(self, chat_id: str) -> Dict[str, Any]:
        return {"name": chat_id, "type": "dm"}


# ── Todo tools ─────────────────────────────────────────────────

def todo_add(args, **kwargs) -> str:
    todos = _load_todos()
    todos.append({"id": len(todos) + 1, "text": args["text"], "done": False})
    _save_todos(todos)
    return json.dumps({"success": True, "todos": todos})


def todo_list(args, **kwargs) -> str:
    todos = _load_todos()
    if not todos:
        return json.dumps({"success": True, "todos": [], "message": "No todos yet."})
    lines = []
    for t in todos:
        mark = "[x]" if t["done"] else "[ ]"
        lines.append(f"{mark} {t['id']}. {t['text']}")
    return json.dumps({"success": True, "todos": todos, "message": "\n".join(lines)})


def todo_complete(args, **kwargs) -> str:
    todos = _load_todos()
    for t in todos:
        if t["id"] == args["id"]:
            t["done"] = True
            _save_todos(todos)
            return json.dumps({"success": True, "todos": todos})
    return json.dumps({"success": False, "error": "Todo not found"})


def todo_delete(args, **kwargs) -> str:
    todos = _load_todos()
    todos = [t for t in todos if t["id"] != args["id"]]
    _save_todos(todos)
    return json.dumps({"success": True, "todos": todos})


# ── Registration ──────────────────────────────────────────────

def check_requirements() -> bool:
    try:
        import websockets
        import aiohttp
        return True
    except ImportError:
        return False


def register(ctx):
    ctx.register_platform(
        name="eink_voice_agent",
        label="E-Ink Voice Agent",
        adapter_factory=lambda cfg: EInkDeviceAdapter(cfg),
        check_fn=check_requirements,
        required_env=[],
        install_hint="pip install websockets aiohttp",
        platform_hint=(
            "You are talking to a user through an E-Ink Voice Agent device. "
            "Keep responses concise (2-3 sentences). For todo commands, use "
            "the todo tools (add, list, complete, delete). "
            "For notes, confirm the saved note text."
        ),
        emoji="🖊️",
    )

    ctx.register_tool(
        name="todo_list",
        toolset="eink_voice_agent",
        schema={
            "name": "todo_list",
            "description": "List all todo items",
            "parameters": {"type": "object", "properties": {}},
        },
        handler=todo_list,
    )
    ctx.register_tool(
        name="todo_add",
        toolset="eink_voice_agent",
        schema={
            "name": "todo_add",
            "description": "Add a new todo item",
            "parameters": {
                "type": "object",
                "properties": {
                    "text": {"type": "string", "description": "Todo text"},
                },
                "required": ["text"],
            },
        },
        handler=todo_add,
    )
    ctx.register_tool(
        name="todo_complete",
        toolset="eink_voice_agent",
        schema={
            "name": "todo_complete",
            "description": "Mark a todo item as done",
            "parameters": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "Todo ID"},
                },
                "required": ["id"],
            },
        },
        handler=todo_complete,
    )
    ctx.register_tool(
        name="todo_delete",
        toolset="eink_voice_agent",
        schema={
            "name": "todo_delete",
            "description": "Delete a todo item",
            "parameters": {
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "Todo ID"},
                },
                "required": ["id"],
            },
        },
        handler=todo_delete,
    )
