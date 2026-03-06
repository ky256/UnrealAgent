"""Project information tools."""

from ..server import mcp, connection
from .python import record_tool_call


@mcp.tool()
async def get_project_info() -> dict:
    """Get detailed information about the current Unreal project.

    Returns project name, engine version, project directory,
    modules list, and enabled plugins.
    """
    record_tool_call("get_project_info")
    return await connection.send_request("get_project_info", {})


@mcp.tool()
async def get_editor_state() -> dict:
    """Get the current Unreal Editor state.

    Returns the active level name, PIE (Play In Editor) status,
    and currently selected actors with their positions.
    """
    record_tool_call("get_editor_state")
    return await connection.send_request("get_editor_state", {})
