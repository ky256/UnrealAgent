"""Python execution tools — universal execution layer."""

import hashlib
import json
import logging
import os
import platform
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

from ..server import mcp, connection
from ..ast_fingerprint import fingerprint_full
from .. import telemetry

logger = logging.getLogger(__name__)

# --- Execution log ---
# Append-only JSONL for Phase 2 pattern recognition.
# Schema is finalized — do not change field names after public release.

_LOG_DIR = Path(__file__).resolve().parent.parent.parent.parent / "Cache"
_LOG_FILE = _LOG_DIR / "execution_log.jsonl"
_LOG_MAX_BYTES = 5 * 1024 * 1024  # 5MB auto-truncate threshold

# --- Session tracking ---

_session_id: str = uuid.uuid4().hex[:8]
_session_epoch: int = 0  # incremented per AI conversation turn
_preceding_tools: list[str] = []  # last N tool calls before this one


def _get_user_id() -> str:
    """Generate a privacy-safe user identifier (hash of username + machine)."""
    raw = f"{os.getenv('USERNAME', os.getenv('USER', 'unknown'))}@{platform.node()}"
    return hashlib.sha256(raw.encode()).hexdigest()[:12]


_user_id: str = _get_user_id()


def increment_session_epoch() -> None:
    """Call this when a new AI conversation turn begins."""
    global _session_epoch
    _session_epoch += 1


def record_tool_call(tool_name: str) -> None:
    """Track preceding tool calls for execution context."""
    _preceding_tools.append(tool_name)
    # Keep only last 10
    if len(_preceding_tools) > 10:
        _preceding_tools.pop(0)


def _truncate_log_if_needed() -> None:
    """Archive and truncate log if it exceeds size limit."""
    try:
        if _LOG_FILE.exists() and _LOG_FILE.stat().st_size > _LOG_MAX_BYTES:
            archive_dir = _LOG_DIR / "archives"
            archive_dir.mkdir(parents=True, exist_ok=True)
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            archive_path = archive_dir / f"exec_{ts}_{_user_id}.jsonl"
            _LOG_FILE.rename(archive_path)
            logger.info(f"Execution log archived to {archive_path}")
    except Exception as e:
        logger.warning(f"Failed to archive execution log: {e}")


def _write_log_entry(entry: dict) -> None:
    """Append a single JSON line to the execution log."""
    try:
        _truncate_log_if_needed()
        _LOG_DIR.mkdir(parents=True, exist_ok=True)
        with open(_LOG_FILE, "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    except Exception as e:
        logger.warning(f"Failed to write execution log: {e}")

    # Also buffer for optional telemetry (no-op if disabled)
    telemetry.record(entry)


@mcp.tool()
async def execute_python(
    code: str,
    timeout_seconds: float = 30.0,
    transaction_name: str = "",
) -> dict:
    """Execute Python code in the Unreal Editor context.

    Has access to the full 'unreal' module API. Use `import unreal` to get started.
    Context is stateful — variables, imports, and function definitions persist across calls.
    Use print() to produce output.
    Operations are wrapped in an undo transaction (Ctrl+Z to revert).

    Args:
        code: Python code to execute. Can be multi-line.
              Example: "import unreal\\nprint(unreal.EditorLevelLibrary.get_all_level_actors())"
        timeout_seconds: Execution timeout in seconds (default 30, max 120).
                         Prevents infinite loops from freezing the editor.
        transaction_name: Human-readable name for the undo transaction.
                          Appears in Edit > Undo History.
                          Example: "Set 12 lights brightness to 5000".
    """
    record_tool_call("execute_python")

    start_time = time.monotonic()
    params = {
        "code": code,
        "timeout_seconds": min(max(timeout_seconds, 1.0), 120.0),
    }
    if transaction_name:
        params["transaction_name"] = transaction_name

    result = await connection.send_request("execute_python", params)
    elapsed_ms = (time.monotonic() - start_time) * 1000

    # Compute AST fingerprint for Phase 2 pattern recognition
    fp_data = fingerprint_full(code)

    # Build complete execution log entry (finalized schema)
    _write_log_entry({
        # Identity
        "id": str(uuid.uuid4()),
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "user_id": _user_id,

        # Session tracking (for Phase 2 decay/consolidation/activity scoring)
        "session_id": _session_id,
        "session_epoch": _session_epoch,

        # Code & AST analysis
        "code": code,
        "code_fingerprint": fp_data["fingerprint"] if fp_data else None,
        "code_template": fp_data["code_template"] if fp_data else None,
        "ast_node_count": fp_data["ast_node_count"] if fp_data else None,
        "parameters_extracted": fp_data["parameters_extracted"] if fp_data else None,

        # Execution result
        "success": result.get("success", False),
        "output": result.get("output", ""),
        "error": result.get("error"),
        "execution_ms": round(elapsed_ms, 1),

        # Context (for intent understanding and tool-chain analysis)
        "context": {
            "preceding_tool_calls": list(_preceding_tools[-5:]),
        },
    })

    return result


@mcp.tool()
async def reset_python_context() -> dict:
    """Reset the shared Python execution context.

    Clears all variables, imports, and function definitions.
    Use this when you want a clean slate.
    """
    record_tool_call("reset_python_context")
    return await connection.send_request("reset_python_context", {})
