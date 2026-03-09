"""Actor manipulation tools."""

from ..server import mcp, connection
from .python import record_tool_call


@mcp.tool()
async def create_actor(
    class_name: str,
    label: str = "",
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 0.0,
    rotation_pitch: float = 0.0,
    rotation_yaw: float = 0.0,
    rotation_roll: float = 0.0,
    scale_x: float = 1.0,
    scale_y: float = 1.0,
    scale_z: float = 1.0,
) -> dict:
    """Create a new actor in the current level.

    Args:
        class_name: Actor class (e.g., StaticMeshActor, PointLight,
                    DirectionalLight, SpotLight, CameraActor).
        label: Display label for the actor.
        location_x: World X coordinate.
        location_y: World Y coordinate.
        location_z: World Z coordinate.
        rotation_pitch: Pitch rotation in degrees.
        rotation_yaw: Yaw rotation in degrees.
        rotation_roll: Roll rotation in degrees.
        scale_x: X scale factor (default 1.0).
        scale_y: Y scale factor (default 1.0).
        scale_z: Z scale factor (default 1.0).
    """
    return await connection.send_request(
        "create_actor",
        {
            "class_name": class_name,
            "label": label,
            "location": {"x": location_x, "y": location_y, "z": location_z},
            "rotation": {
                "pitch": rotation_pitch,
                "yaw": rotation_yaw,
                "roll": rotation_roll,
            },
            "scale": {"x": scale_x, "y": scale_y, "z": scale_z},
        },
    )


@mcp.tool()
async def delete_actor(actor_name: str) -> dict:
    """Delete an actor from the current level.

    Args:
        actor_name: The label or internal name of the actor to delete.
    """
    return await connection.send_request(
        "delete_actor", {"actor_name": actor_name}
    )


@mcp.tool()
async def select_actors(actor_names: list[str]) -> dict:
    """Select specific actors in the editor.

    Args:
        actor_names: List of actor label/names to select.
                    Pass an empty list to clear the selection.
    """
    record_tool_call("select_actors")
    return await connection.send_request(
        "select_actors", {"actor_names": actor_names}
    )
