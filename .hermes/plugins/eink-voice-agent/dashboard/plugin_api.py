"""FastAPI router for the E-Ink Voice Agent dashboard plugin.

Mounted at /api/plugins/eink-voice-agent/ by the Hermes dashboard.
Shows device activity, todos, and notes managed by the plugin.
"""
import json
import logging
from datetime import datetime
from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

logger = logging.getLogger(__name__)

router = APIRouter()

# ── Shared state (mirrors adapter.py conventions) ───────────────────
NOTES_DIR = Path.home() / ".eink-voice-agent" / "notes"
TODOS_FILE = Path.home() / ".eink-voice-agent" / "todos.json"
ACTIVITY_LOG = Path.home() / ".eink-voice-agent" / "activity.jsonl"
TELEMETRY_FILE = Path.home() / ".eink-voice-agent" / "telemetry.json"


# ── Helpers ─────────────────────────────────────────────────────────

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
    """Read activity log written by the adapter, newest first."""
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


# ── Models ──────────────────────────────────────────────────────────

class StatusResponse(BaseModel):
    port: int
    host: str
    listening: bool


class TodoItem(BaseModel):
    id: int
    text: str
    done: bool


class TodosResponse(BaseModel):
    total: int
    done: int
    pending: int
    items: list[TodoItem]


class TodoCreate(BaseModel):
    text: str


class NoteInfo(BaseModel):
    name: str
    preview: str
    modified: str
    size: int


class NoteContent(BaseModel):
    name: str
    content: str


class ActivityEntry(BaseModel):
    type: str
    ts: str
    detail: str | None = None
    direction: str | None = None
    content: str | None = None
    session_id: str | None = None


class TelemetryData(BaseModel):
    battery: int | None = None
    charging: bool | None = None
    wifi_rssi: int | None = None
    uptime: int | None = None
    wake_count: int | None = None
    storage_free_kb: int | None = None
    recording_time_remaining_sec: int | None = None
    updated_at: str | None = None


class TelemetryResponse(BaseModel):
    available: bool
    data: TelemetryData | None = None


class TranscriptEntry(BaseModel):
    ts: str
    direction: str
    content: str
    session_id: str | None = None


# ── Routes ──────────────────────────────────────────────────────────

@router.get("/status", response_model=StatusResponse)
async def get_status():
    """Report config and whether the WebSocket server port is open."""
    import os
    import socket

    port = int(os.getenv("EINK_DEVICE_PORT", "8123"))
    host = os.getenv("EINK_DEVICE_HOST", "0.0.0.0")

    listening = False
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1)
        listening = s.connect_ex(("127.0.0.1", port)) == 0
        s.close()
    except Exception:
        pass

    return StatusResponse(port=port, host=host, listening=listening)


@router.get("/todos", response_model=TodosResponse)
async def get_todos():
    items = _load_todos()
    total, done, pending = _count_todos(items)
    return TodosResponse(total=total, done=done, pending=pending, items=items)


@router.post("/todos", response_model=TodosResponse)
async def add_todo(body: TodoCreate):
    items = _load_todos()
    next_id = max((t["id"] for t in items), default=0) + 1
    items.append({"id": next_id, "text": body.text, "done": False})
    _save_todos(items)
    total, done, pending = _count_todos(items)
    return TodosResponse(total=total, done=done, pending=pending, items=items)


@router.post("/todos/{todo_id}/toggle", response_model=TodosResponse)
async def toggle_todo(todo_id: int):
    items = _load_todos()
    for t in items:
        if t["id"] == todo_id:
            t["done"] = not t["done"]
            _save_todos(items)
            break
    total, done, pending = _count_todos(items)
    return TodosResponse(total=total, done=done, pending=pending, items=items)


@router.delete("/todos/{todo_id}", response_model=TodosResponse)
async def delete_todo(todo_id: int):
    items = [t for t in _load_todos() if t["id"] != todo_id]
    _save_todos(items)
    total, done, pending = _count_todos(items)
    return TodosResponse(total=total, done=done, pending=pending, items=items)


@router.get("/notes", response_model=list[NoteInfo])
async def list_notes():
    return _recent_notes(limit=15)


@router.get("/notes/{filename}", response_model=NoteContent)
async def get_note(filename: str):
    safe = Path(filename).name
    path = NOTES_DIR / safe
    if not path.exists() or not path.is_file():
        raise HTTPException(status_code=404, detail="Note not found")
    return NoteContent(name=safe, content=path.read_text())


@router.delete("/notes/{filename}")
async def delete_note(filename: str):
    safe = Path(filename).name
    path = NOTES_DIR / safe
    if not path.exists() or not path.is_file():
        raise HTTPException(status_code=404, detail="Note not found")
    path.unlink()
    return {"ok": True}


@router.get("/activity", response_model=list[ActivityEntry])
async def list_activity():
    return _recent_activity(limit=50)


@router.get("/telemetry", response_model=TelemetryResponse)
async def get_telemetry():
    if not TELEMETRY_FILE.exists():
        return TelemetryResponse(available=False)
    try:
        data = json.loads(TELEMETRY_FILE.read_text())
        if data.get("storage_free_kb") is not None:
            data["recording_time_remaining_sec"] = int(data["storage_free_kb"] / 32)
        return TelemetryResponse(available=True, data=TelemetryData(**data))
    except Exception:
        return TelemetryResponse(available=False)


@router.get("/transcript", response_model=list[TranscriptEntry])
async def get_transcript(limit: int = 30):
    entries = _recent_activity(limit=100)
    messages = [
        TranscriptEntry(
            ts=e["ts"],
            direction=e["direction"],
            content=e.get("content", ""),
            session_id=e.get("session_id"),
        )
        for e in entries
        if e.get("type") == "message" and e.get("direction")
    ]
    return messages[-limit:]
