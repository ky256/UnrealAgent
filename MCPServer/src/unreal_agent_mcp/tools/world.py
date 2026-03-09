"""World and level tools."""

from ..server import mcp, connection
from .python import record_tool_call


@mcp.tool()
async def get_world_outliner(class_filter: str = "") -> dict:
    """Get all actors in the current level (world outliner).

    Args:
        class_filter: Optional class name filter (case-insensitive).

    Returns a list of actors with name, class, location, rotation,
    scale, and visibility status.
    """
    params = {}
    if class_filter:
        params["class_filter"] = class_filter
    return await connection.send_request("get_world_outliner", params)


@mcp.tool()
async def get_current_level() -> dict:
    """Get information about the current level.

    Returns level name, path, and streaming sub-levels with
    their loaded/visible status.
    """
    return await connection.send_request("get_current_level", {})


@mcp.tool()
async def get_actor_details(actor_name: str) -> dict:
    """Get detailed properties of a specific actor.

    Args:
        actor_name: The label or internal name of the actor.

    Returns full transform, components list, tags, and flags.
    """
    record_tool_call("get_actor_details")
    return await connection.send_request(
        "get_actor_details", {"actor_name": actor_name}
    )
