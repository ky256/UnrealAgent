"""Asset management tools."""

from ..server import mcp, connection
from .python import record_tool_call


@mcp.tool()
async def list_assets(
    path: str = "/Game",
    class_filter: str = "",
    recursive: bool = True,
) -> dict:
    """List assets in the Unreal project.

    Args:
        path: Asset path to list (e.g., /Game/Blueprints). Defaults to /Game.
        class_filter: Filter by class name (e.g., Blueprint, StaticMesh, Material).
        recursive: Search recursively in subdirectories. Defaults to True.
    """
    params = {"path": path, "recursive": recursive}
    if class_filter:
        params["class_filter"] = class_filter
    return await connection.send_request("list_assets", params)


@mcp.tool()
async def search_assets(
    query: str,
    class_filter: str = "",
) -> dict:
    """Search for assets by name.

    Args:
        query: Search string to match against asset names (case-insensitive).
        class_filter: Optional class name filter (e.g., Blueprint, Material).
    """
    params = {"query": query}
    if class_filter:
        params["class_filter"] = class_filter
    return await connection.send_request("search_assets", params)


@mcp.tool()
async def get_asset_info(asset_path: str) -> dict:
    """Get detailed information about a specific asset.

    Args:
        asset_path: Full asset path (e.g., /Game/Blueprints/BP_Player).

    Returns asset name, class, package, and metadata tags.
    """
    return await connection.send_request("get_asset_info", {"asset_path": asset_path})


@mcp.tool()
async def get_asset_references(asset_path: str) -> dict:
    """Get the reference graph for an asset.

    Args:
        asset_path: Full asset path to query.

    Returns lists of referencers (what uses this asset)
    and dependencies (what this asset uses).
    """
    record_tool_call("get_asset_references")
    return await connection.send_request(
        "get_asset_references", {"asset_path": asset_path}
    )
