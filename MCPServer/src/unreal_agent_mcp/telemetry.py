"""Optional telemetry for the evolution system data flywheel.

Design principles:
- OFF by default — user must explicitly opt in
- Privacy-safe — only sends anonymized execution patterns, never raw code content
- Transparent — user can inspect exactly what is sent via the log file
- Controllable — can be toggled at any time via environment variable or config

Environment variables:
    UNREAL_AGENT_TELEMETRY=1        Enable telemetry
    UNREAL_AGENT_TELEMETRY_URL=...  Custom endpoint (default: official server)
"""

import asyncio
import json
import logging
import os
from pathlib import Path

logger = logging.getLogger(__name__)

# --- Configuration ---

# Telemetry is OFF by default. Must be explicitly enabled.
_ENABLED = os.environ.get("UNREAL_AGENT_TELEMETRY", "0").strip() in ("1", "true", "yes")

# Default endpoint — placeholder until official server is set up
_DEFAULT_URL = "https://telemetry.unrealai.dev/v1/collect"
_ENDPOINT = os.environ.get("UNREAL_AGENT_TELEMETRY_URL", _DEFAULT_URL).strip()

# Batch settings
_BATCH_SIZE = 20  # Send in batches of 20 entries
_FLUSH_INTERVAL_S = 300  # Or every 5 minutes

# Buffer
_buffer: list[dict] = []
_flush_task: asyncio.Task | None = None


def is_enabled() -> bool:
    """Check if telemetry is enabled."""
    return _ENABLED


def _sanitize_entry(entry: dict) -> dict:
    """Strip sensitive fields before sending.

    We only send structural data (fingerprints, stats), never raw code.
    """
    return {
        # Identity (anonymized)
        "user_id": entry.get("user_id"),
        "session_id": entry.get("session_id"),
        "session_epoch": entry.get("session_epoch"),

        # Structural data only — no raw code
        "code_fingerprint": entry.get("code_fingerprint"),
        "code_template": entry.get("code_template"),
        "ast_node_count": entry.get("ast_node_count"),
        "parameters_extracted": entry.get("parameters_extracted"),

        # Execution stats
        "success": entry.get("success"),
        "execution_ms": entry.get("execution_ms"),

        # Context
        "context": entry.get("context"),

        # Timestamp
        "timestamp": entry.get("timestamp"),
    }


async def _do_flush() -> None:
    """Send buffered entries to the telemetry endpoint."""
    global _buffer

    if not _buffer:
        return

    batch = _buffer[:_BATCH_SIZE]
    _buffer = _buffer[_BATCH_SIZE:]

    sanitized = [_sanitize_entry(e) for e in batch]

    try:
        # Use aiohttp if available, fall back to urllib
        try:
            import aiohttp
            async with aiohttp.ClientSession() as session:
                async with session.post(
                    _ENDPOINT,
                    json={"entries": sanitized},
                    timeout=aiohttp.ClientTimeout(total=10),
                ) as resp:
                    if resp.status == 200:
                        logger.debug(f"Telemetry: sent {len(sanitized)} entries")
                    else:
                        logger.debug(f"Telemetry: server returned {resp.status}")
        except ImportError:
            # Fallback: synchronous urllib in executor
            import urllib.request
            data = json.dumps({"entries": sanitized}).encode("utf-8")
            req = urllib.request.Request(
                _ENDPOINT,
                data=data,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(
                None,
                lambda: urllib.request.urlopen(req, timeout=10),
            )
            logger.debug(f"Telemetry: sent {len(sanitized)} entries (urllib)")
    except Exception as e:
        # Telemetry failures are never critical — just log and move on
        logger.debug(f"Telemetry: failed to send ({e})")


async def _periodic_flush() -> None:
    """Background task that periodically flushes the buffer."""
    while True:
        await asyncio.sleep(_FLUSH_INTERVAL_S)
        await _do_flush()


def record(entry: dict) -> None:
    """Buffer a log entry for telemetry upload.

    Only works if telemetry is enabled. No-op otherwise.
    """
    if not _ENABLED:
        return

    _buffer.append(entry)

    # Start background flush task if not running
    global _flush_task
    if _flush_task is None or _flush_task.done():
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                _flush_task = loop.create_task(_periodic_flush())
        except RuntimeError:
            pass

    # Flush immediately if buffer is full
    if len(_buffer) >= _BATCH_SIZE:
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                loop.create_task(_do_flush())
        except RuntimeError:
            pass
