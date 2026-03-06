"""Viewport and camera tools."""

from ..server import mcp, connection


@mcp.tool()
async def get_viewport_camera() -> dict:
    """Get the current editor viewport camera position and rotation.

    Returns the camera's world location (x, y, z) and
    rotation (pitch, yaw, roll) in degrees.
    """
    return await connection.send_request("get_viewport_camera", {})


@mcp.tool()
async def move_viewport_camera(
    location_x: float | None = None,
    location_y: float | None = None,
    location_z: float | None = None,
    rotation_pitch: float | None = None,
    rotation_yaw: float | None = None,
    rotation_roll: float | None = None,
) -> dict:
    """Move the editor viewport camera to a specific position.

    Only provided values will be changed; others keep their current value.

    Args:
        location_x: Camera X position.
        location_y: Camera Y position.
        location_z: Camera Z position.
        rotation_pitch: Camera pitch in degrees.
        rotation_yaw: Camera yaw in degrees.
        rotation_roll: Camera roll in degrees.
    """
    params = {}
    if location_x is not None:
        params["location_x"] = location_x
    if location_y is not None:
        params["location_y"] = location_y
    if location_z is not None:
        params["location_z"] = location_z
    if rotation_pitch is not None:
        params["rotation_pitch"] = rotation_pitch
    if rotation_yaw is not None:
        params["rotation_yaw"] = rotation_yaw
    if rotation_roll is not None:
        params["rotation_roll"] = rotation_roll
    return await connection.send_request("move_viewport_camera", params)


@mcp.tool()
async def focus_on_actor(actor_name: str) -> dict:
    """Focus the editor viewport camera on a specific actor.

    Args:
        actor_name: The label or name of the actor to focus on.
    """
    record_tool_call("focus_on_actor")
    return await connection.send_request(
        "focus_on_actor", {"actor_name": actor_name}
    )
