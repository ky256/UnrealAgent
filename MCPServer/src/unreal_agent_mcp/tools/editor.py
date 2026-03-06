"""Editor undo/redo tools."""

from ..server import mcp, connection
from .python import record_tool_call


@mcp.tool()
async def undo(steps: int = 1) -> dict:
    """Undo the last editor operation(s).

    Works on all operations including those performed by execute_python.
    Each execute_python call is a single undo step.

    Args:
        steps: Number of steps to undo (default 1, max 20).
    """
    record_tool_call("undo")
    return await connection.send_request("undo", {"steps": min(max(steps, 1), 20)})


@mcp.tool()
async def redo(steps: int = 1) -> dict:
    """Redo the last undone operation(s).

    Args:
        steps: Number of steps to redo (default 1, max 20).
    """
    record_tool_call("redo")
    return await connection.send_request("redo", {"steps": min(max(steps, 1), 20)})
