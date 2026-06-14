import asyncio
import json
import logging
import os
from urllib.parse import urlparse

from gateway.platforms.base import (
    BasePlatformAdapter,
    MessageEvent,
    MessageType,
    SendResult,
)
from gateway.config import Platform, PlatformConfig

logger = logging.getLogger(__name__)


class EInkDeviceAdapter(BasePlatformAdapter):
    def __init__(self, config: PlatformConfig):
        super().__init__(config, Platform("eink_voice_agent"))
        self._port = int(os.getenv("EINK_DEVICE_PORT", "8123"))
        self._host = os.getenv("EINK_DEVICE_HOST", "0.0.0.0")
        self._server = None
        self._connections: dict[str, asyncio.Queue] = {}

    async def connect(self) -> bool:
        try:
            import websockets
        except ImportError:
            logger.error("websockets library not installed — run: pip install websockets")
            return False

        self._server = await websockets.serve(
            self._handle_ws,
            self._host,
            self._port,
        )
        logger.info("E-Ink device server listening on ws://%s:%s", self._host, self._port)
        self._mark_connected()
        return True

    async def disconnect(self) -> None:
        if self._server:
            self._server.close()
            await self._server.wait_closed()
        self._mark_disconnected()

    async def _handle_ws(self, ws):
        device_id = None
        try:
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    await ws.send(json.dumps({"type": "error", "data": "invalid json"}))
                    continue

                msg_type = msg.get("type", "")
                msg_data = msg.get("data", "")

                if msg_type == "auth":
                    device_id = msg.get("device_id", "unknown")
                    chat_id = f"eink:{device_id}"
                    self._connections[chat_id] = asyncio.Queue()
                    logger.info("Device '%s' authenticated", device_id)
                    await ws.send(json.dumps({"type": "auth_ok"}))

                elif msg_type in ("audio", "text"):
                    if not device_id:
                        await ws.send(json.dumps({"type": "error", "data": "not authenticated"}))
                        continue

                    chat_id = f"eink:{device_id}"
                    source = self.build_source(
                        chat_id=chat_id,
                        chat_name=f"Device-{device_id}",
                        chat_type="dm",
                        user_id=device_id,
                        user_name=device_id,
                    )
                    event = MessageEvent(
                        text=msg_data if msg_type == "text" else "[audio input]",
                        message_type=MessageType.TEXT,
                        source=source,
                        message_id=msg.get("session_id", ""),
                        extra_data={"audio_data": msg_data} if msg_type == "audio" else {},
                    )

                    async def deliver(msg=raw):
                        self._connections[chat_id].put_nowait(msg)

                    self._response_queues[chat_id] = self._connections[chat_id]
                    await self.handle_message(event)

                elif msg_type == "ping":
                    await ws.send(json.dumps({"type": "pong"}))

        except websockets.exceptions.ConnectionClosed:
            logger.info("Device '%s' disconnected", device_id or "unknown")
        finally:
            if device_id:
                chat_id = f"eink:{device_id}"
                self._connections.pop(chat_id, None)
                self._response_queues.pop(chat_id, None)

    async def send(self, chat_id, content, reply_to=None, metadata=None):
        queue = self._connections.get(chat_id)
        if not queue:
            return SendResult(success=False, message_id="")
        payload = json.dumps({"type": "response", "data": content})
        await queue.put(payload)
        return SendResult(success=True, message_id="")

    async def send_audio(self, chat_id, audio_base64, reply_to=None, metadata=None):
        queue = self._connections.get(chat_id)
        if not queue:
            return SendResult(success=False, message_id="")
        payload = json.dumps({"type": "audio_response", "data": audio_base64})
        await queue.put(payload)
        return SendResult(success=True, message_id="")


def check_requirements() -> bool:
    try:
        import websockets
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
        install_hint="pip install websockets",
        platform_hint=(
            "You are talking to a user through an E-Ink Voice Agent device. "
            "The user speaks to a physical device with a microphone and speaker. "
            "Keep responses concise (2-3 sentences max) since the device has a "
            "200x200 pixel e-ink display. Do not use markdown formatting."
        ),
        emoji="🖊️",
    )
