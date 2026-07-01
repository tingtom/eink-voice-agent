import asyncio
import json
import logging
import os
import socket
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
    now = datetime.now(timezone.utc).isoformat()
    data["last_seen"] = now
    data.setdefault("updated_at", now)
    TELEMETRY_FILE.write_text(json.dumps(data, indent=2))


def _load_todos():
    if TODOS_FILE.exists():
        return json.loads(TODOS_FILE.read_text())
    return []


def _save_todos(todos):
    TODOS_FILE.parent.mkdir(parents=True, exist_ok=True)
    TODOS_FILE.write_text(json.dumps(todos, indent=2))


def _count_todos(todos):
    total = len(todos)
    done = sum(1 for t in todos if t.get("done"))
    return total, done, total - done


def _load_telemetry():
    if TELEMETRY_FILE.exists():
        return json.loads(TELEMETRY_FILE.read_text())
    return {}


def _recent_notes(limit=10):
    if not NOTES_DIR.exists():
        return []
    files = sorted(NOTES_DIR.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True)
    result = []
    for f in files[:limit]:
        if not f.is_file():
            continue
        preview = f.read_text().strip()[:120]
        result.append({
            "name": f.name,
            "preview": preview,
            "modified": datetime.fromtimestamp(f.stat().st_mtime).isoformat(),
            "size": f.stat().st_size,
        })
    return result


def _recent_activity(limit=50):
    if not ACTIVITY_LOG.exists():
        return []
    lines = ACTIVITY_LOG.read_text().strip().splitlines()
    entries = []
    for line in reversed(lines[-limit:]):
        try:
            entries.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return entries


def _transcribe_audio(audio_base64: str, sample_rate: int = 16000) -> str:
    try:
        with open("/tmp/eink_audio_raw.bin", "wb") as f:
            f.write(__import__("base64").b64decode(audio_base64))

        result = subprocess.run(
            ["ffmpeg", "-y", "-f", "s16le", "-ar", str(sample_rate), "-ac", "1",
             "-i", "/tmp/eink_audio_raw.bin",
             "/tmp/eink_audio.wav"],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            logger.warning("ffmpeg failed: %s", result.stderr)
            return "[transcription failed]"

        result = subprocess.run(
            ["whisper", "/tmp/eink_audio.wav", "--language", "en",
             "--output_dir", "/tmp/", "--output_format", "txt", "--model", "base"],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            logger.warning("whisper failed: %s", result.stderr)
            return "[transcription failed]"

        txt_path = Path("/tmp/eink_audio.txt")
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
        self._audio_start_times: dict[str, float] = {}
        self._session_device_map: dict[str, str] = {}
        self._ws_by_chat: dict[str, object] = {}
        self._device_chat_map: dict[str, str] = {}
        self._pending_responses: dict[str, str] = {}
        _ensure_dirs()

    async def connect(self) -> bool:
        # Advertise via mDNS so ESP32 can discover us
        self._zeroconf = None
        try:
            from zeroconf import Zeroconf, ServiceInfo
            import socket
            
            # Get the LAN IP by connecting to a known address
            host_ip = "0.0.0.0"
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.connect(("8.8.8.8", 80))
                host_ip = s.getsockname()[0]
                s.close()
            except Exception:
                pass
            
            # Fallback to hostname resolution
            if host_ip == "0.0.0.0" or host_ip.startswith("127."):
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

        # CORS middleware for device HTTP API
        @web.middleware
        async def cors_middleware(request, handler):
            if request.method == "OPTIONS":
                return web.Response(
                    status=200,
                    headers={
                        "Access-Control-Allow-Origin": request.headers.get("Origin", "*"),
                        "Access-Control-Allow-Credentials": "true",
                        "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
                        "Access-Control-Allow-Headers": "Content-Type, Authorization, X-Device-ID",
                        "Access-Control-Max-Age": "86400",
                    }
                )
            origin = request.headers.get("Origin")
            response = await handler(request)
            if origin:
                response.headers["Access-Control-Allow-Origin"] = origin
                response.headers["Access-Control-Allow-Credentials"] = "true"
            else:
                response.headers["Access-Control-Allow-Origin"] = "*"
            response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
            response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Device-ID"
            return response

        app = web.Application(middlewares=[cors_middleware])
        app.add_routes([
            web.post("/api/device/auth", self._handle_auth),
            web.post("/api/device/audio", self._handle_audio),
            web.post("/api/device/audio/end", self._handle_audio_end),
            web.post("/api/device/telemetry", self._handle_telemetry),
            web.get("/api/device/response", self._handle_poll_response),
            # Dashboard plugin API routes
            web.get("/api/plugins/eink-voice-agent/status", self._handle_plugin_status),
            web.get("/api/plugins/eink-voice-agent/todos", self._handle_plugin_todos),
            web.post("/api/plugins/eink-voice-agent/todos", self._handle_plugin_todos_add),
            web.post("/api/plugins/eink-voice-agent/todos/{todo_id}/toggle", self._handle_plugin_todo_toggle),
            web.delete("/api/plugins/eink-voice-agent/todos/{todo_id}", self._handle_plugin_todo_delete),
            web.get("/api/plugins/eink-voice-agent/notes", self._handle_plugin_notes),
            web.get("/api/plugins/eink-voice-agent/notes/{filename}", self._handle_plugin_note_get),
            web.delete("/api/plugins/eink-voice-agent/notes/{filename}", self._handle_plugin_note_delete),
            web.get("/api/plugins/eink-voice-agent/activity", self._handle_plugin_activity),
            web.get("/api/plugins/eink-voice-agent/telemetry", self._handle_plugin_telemetry),
            web.get("/api/plugins/eink-voice-agent/transcript", self._handle_plugin_transcript),
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
            logger.warning("Device not authenticated: %s", device_id)
            return web.json_response({"error": "not authenticated"}, status=401)

        session = request.query.get("session_id", chat_id)
        mode = request.query.get("mode", "agent")
        body = await request.read()

        import base64, time
        chunk_b64 = base64.b64encode(body).decode("ascii")
        if session not in self._audio_buffers:
            self._audio_buffers[session] = []
            self._audio_start_times[session] = time.monotonic()
        self._audio_buffers[session].append(chunk_b64)
        self._session_device_map[session] = device_id
        return web.json_response({"status": "ok"})

    async def _handle_audio_end(self, request):
        device_id = request.headers.get("X-Device-ID", "unknown")
        chat_id = self._device_chat_map.get(device_id)

        try:
            body = await request.json()
        except Exception:
            return web.json_response({"error": "invalid json"}, status=400)

        session = body.get("session_id")
        # If X-Device-ID didn't match, try resolving from session_id
        if not chat_id and session and session in self._session_device_map:
            device_id = self._session_device_map[session]
            chat_id = self._device_chat_map.get(device_id)

        if not chat_id:
            return web.json_response({"error": "not authenticated"}, status=401)

        if not session:
            session = chat_id
        mode = body.get("mode", "agent")

        # Wait briefly for in-flight audio chunks that left the firmware
        # after recording stopped but before the ring buffer drain completed.
        await asyncio.sleep(0.5)

        chunks = self._audio_buffers.pop(session, [])
        # Each chunk is independently base64-encoded; concatenate decoded bytes
        import base64
        raw_parts = [base64.b64decode(c) for c in chunks]
        full_audio = base64.b64encode(b"".join(raw_parts)).decode("ascii")
        logger.info("Popped %d audio chunks for session=%s", len(chunks), session)

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

    async def _handle_poll_response(self, request):
        chat_id = request.query.get("chat_id", "unknown")
        content = self._pending_responses.pop(chat_id, None)
        if content is None:
            return web.json_response({"type": "no_response"})
        content = content.replace("```", "`").replace("  ", " ")  # sanitize for device
        return web.json_response({"type": "response", "data": content})

    async def _process_end(self, session, mode, full_audio, chat_id, device_id):
        # Calculate actual sample rate from timing
        import base64 as _b64, time as _t
        raw_len = len(_b64.b64decode(full_audio))
        total_samples = raw_len // 2  # 16-bit = 2 bytes per sample
        start_time = self._audio_start_times.pop(session, None)
        elapsed = (_t.monotonic() - start_time) if start_time else 0
        actual_sr = int(total_samples / elapsed) if elapsed > 0 else 16000
        # Clamp to reasonable range
        actual_sr = max(8000, min(48000, actual_sr))
        logger.info("Audio timing: %d samples, %.2fs elapsed, estimated %d Hz",
                     total_samples, elapsed, actual_sr)

        if mode == "transcribe":
            text = _transcribe_audio(full_audio, sample_rate=actual_sr)
            timestamp = __import__("datetime").datetime.now().strftime(
                "%Y-%m-%d_%H-%M-%S"
            )
            note_file = NOTES_DIR / f"note_{timestamp}.txt"
            note_file.write_text(text)
            telemetry = _load_telemetry()
            _log_activity({
                "type": "message", "direction": "in",
                "content": f"[voice note] {text[:500]}".strip(),
                "session_id": session,
                "battery": telemetry.get("battery"),
                "charging": telemetry.get("charging"),
                "wifi_rssi": telemetry.get("wifi_rssi"),
            })
            reply = f"Transcribed note saved:\n\n{text[:180]}"
            self._pending_responses[chat_id] = reply
            return reply

        elif mode == "todo":
            if full_audio:
                text = _transcribe_audio(full_audio, sample_rate=actual_sr)
            else:
                text = "list my todos"
            source = self.build_source(
                chat_id=chat_id, chat_name=f"Device-{device_id}",
                chat_type="dm", user_id=device_id, user_name=device_id,
                role_authorized=True,
            )
            event = MessageEvent(
                text=text,
                message_type=MessageType.TEXT,
                source=source,
                message_id=session,
            )
            await self.handle_message(event)
            telemetry = _load_telemetry()
            _log_activity({
                "type": "message", "direction": "in",
                "content": text,
                "session_id": session,
                "battery": telemetry.get("battery"),
                "charging": telemetry.get("charging"),
                "wifi_rssi": telemetry.get("wifi_rssi"),
            })
            # Return a simple acknowledgment
            return "Todo command processed"

        else:
            # Write audio to a proper WAV file so the pipeline's media processing
            # (_enrich_message_with_transcription / faster_whisper) can read it.
            import tempfile
            import base64
            import wave
            raw = base64.b64decode(full_audio)
            tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
            with wave.open(tmp.name, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(actual_sr)
                wf.writeframes(raw)
            tmp.close()

            source = self.build_source(
                chat_id=chat_id, chat_name=f"Device-{device_id}",
                chat_type="dm", user_id=device_id, user_name=device_id,
                role_authorized=True,
            )
            event = MessageEvent(
                text="[agent audio input]",
                message_type=MessageType.VOICE,
                source=source,
                message_id=session,
                media_urls=[tmp.name],
                media_types=["audio/wav"],
            )

            # For agent mode: wait for response synchronously.
            # Spawn session processing and poll for the response.
            self._start_session_processing(event, chat_id)

            # Poll for response with extended timeout (60s)
            for i in range(120):
                await asyncio.sleep(0.5)
                response = self._pending_responses.pop(chat_id, None)
                if response:
                    logger.info("Got LLM response after %d polls", i)
                    return response

            # Timeout — return acknowledgment
            logger.info("Audio received (response pending)")

            telemetry = _load_telemetry()
            _log_activity({
                "type": "message", "direction": "in",
                "content": "[agent audio input]",
                "session_id": session,
                "battery": telemetry.get("battery"),
                "charging": telemetry.get("charging"),
                "wifi_rssi": telemetry.get("wifi_rssi"),
            })
            return "Audio received"

    async def send(self, chat_id, content, reply_to=None, metadata=None):
        logger.info("send() called: chat_id=%s, content=%.60s", chat_id, content[:100] if content else "")
        self._pending_responses[chat_id] = content
        telemetry = _load_telemetry()
        _log_activity({
            "type": "message", "direction": "out",
            "content": content[:500],
            "session_id": chat_id,
            "battery": telemetry.get("battery"),
            "charging": telemetry.get("charging"),
            "wifi_rssi": telemetry.get("wifi_rssi"),
        })
        return SendResult(success=True, message_id="")

    async def get_chat_info(self, chat_id: str) -> Dict[str, Any]:
        return {"name": chat_id, "type": "dm"}

    # ── Dashboard plugin API handlers ─────────────────────────────

    async def _handle_plugin_status(self, request):
        listening = self._http_server is not None
        return web.json_response({
            "port": self._port, "host": self._host, "listening": listening,
        })

    async def _handle_plugin_todos(self, request):
        items = _load_todos()
        total, done, pending = _count_todos(items)
        return web.json_response({
            "total": total, "done": done, "pending": pending, "items": items,
        })

    async def _handle_plugin_todos_add(self, request):
        try:
            body = await request.json()
        except Exception:
            return web.json_response({"error": "invalid json"}, status=400)
        items = _load_todos()
        next_id = max((t["id"] for t in items), default=0) + 1
        items.append({"id": next_id, "text": body.get("text", ""), "done": False})
        _save_todos(items)
        total, done, pending = _count_todos(items)
        return web.json_response({
            "total": total, "done": done, "pending": pending, "items": items,
        })

    async def _handle_plugin_todo_toggle(self, request):
        try:
            todo_id = int(request.match_info["todo_id"])
        except (ValueError, KeyError):
            return web.json_response({"error": "invalid todo id"}, status=400)
        items = _load_todos()
        for t in items:
            if t["id"] == todo_id:
                t["done"] = not t["done"]
                _save_todos(items)
                break
        total, done, pending = _count_todos(items)
        return web.json_response({
            "total": total, "done": done, "pending": pending, "items": items,
        })

    async def _handle_plugin_todo_delete(self, request):
        try:
            todo_id = int(request.match_info["todo_id"])
        except (ValueError, KeyError):
            return web.json_response({"error": "invalid todo id"}, status=400)
        items = [t for t in _load_todos() if t["id"] != todo_id]
        _save_todos(items)
        total, done, pending = _count_todos(items)
        return web.json_response({
            "total": total, "done": done, "pending": pending, "items": items,
        })

    async def _handle_plugin_notes(self, request):
        return web.json_response(_recent_notes(limit=15))

    async def _handle_plugin_note_get(self, request):
        filename = Path(request.match_info["filename"]).name
        path = NOTES_DIR / filename
        if not path.exists() or not path.is_file():
            return web.json_response({"error": "Note not found"}, status=404)
        return web.json_response({"name": filename, "content": path.read_text()})

    async def _handle_plugin_note_delete(self, request):
        filename = Path(request.match_info["filename"]).name
        path = NOTES_DIR / filename
        if not path.exists() or not path.is_file():
            return web.json_response({"error": "Note not found"}, status=404)
        path.unlink()
        return web.json_response({"ok": True})

    async def _handle_plugin_activity(self, request):
        return web.json_response(_recent_activity(limit=50))

    async def _handle_plugin_telemetry(self, request):
        if not TELEMETRY_FILE.exists():
            return web.json_response({"available": False})
        try:
            data = json.loads(TELEMETRY_FILE.read_text())
            if data.get("storage_free_kb") is not None:
                data["recording_time_remaining_sec"] = int(data["storage_free_kb"] / 32)
            return web.json_response({"available": True, "data": data})
        except Exception:
            return web.json_response({"available": False})

    async def _handle_plugin_transcript(self, request):
        limit = int(request.query.get("limit", "30"))
        entries = _recent_activity(limit=100)
        messages = [
            {
                "ts": e["ts"],
                "direction": e["direction"],
                "content": e.get("content", ""),
                "session_id": e.get("session_id"),
                "battery": e.get("battery"),
                "charging": e.get("charging"),
                "wifi_rssi": e.get("wifi_rssi"),
            }
            for e in entries
            if e.get("type") == "message" and e.get("direction")
        ]
        return web.json_response(messages[-limit:][::-1])


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
        cron_deliver_env_var="EINK_VOICE_AGENT_HOME_CHANNEL",
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
