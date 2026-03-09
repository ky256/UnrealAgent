"""Asset management tools — create, duplicate, rename, delete, save assets."""

from ..server import mcp, connection


@mcp.tool()
async def create_asset(
    asset_name: str,
    package_path: str,
    asset_class: str,
    parent_material: str = "",
    parent_class: str = "",
) -> dict:
    """创建新资产。

    Args:
        asset_name: 资产名称
        package_path: 包路径，如 /Game/Materials
        asset_class: 资产类型: Material, MaterialInstance, Blueprint
        parent_material: MaterialInstance 的父材质路径
        parent_class: Blueprint 的父类名（如 Actor, Character），默认 Actor
    """
    params: dict = {
        "asset_name": asset_name,
        "package_path": package_path,
        "asset_class": asset_class,
    }
    if parent_material:
        params["parent_material"] = parent_material
    if parent_class:
        params["parent_class"] = parent_class
    return await connection.send_request("create_asset", params)


@mcp.tool()
async def duplicate_asset(source_path: str, dest_path: str, new_name: str) -> dict:
    """复制资产到新位置。

    Args:
        source_path: 源资产路径
        dest_path: 目标包路径（文件夹）
        new_name: 新资产名称
    """
    return await connection.send_request(
        "duplicate_asset",
        {"source_path": source_path, "dest_path": dest_path, "new_name": new_name},
    )


@mcp.tool()
async def rename_asset(
    asset_path: str,
    new_name: str = "",
    new_path: str = "",
) -> dict:
    """重命名或移动资产。

    Args:
        asset_path: 当前资产路径
        new_name: 新名称，可选
        new_path: 新目标文件夹路径，可选
    """
    params: dict = {"asset_path": asset_path}
    if new_name:
        params["new_name"] = new_name
    if new_path:
        params["new_path"] = new_path
    return await connection.send_request("rename_asset", params)


@mcp.tool()
async def delete_asset(asset_path: str, force: bool = False) -> dict:
    """删除资产。默认安全模式：如有引用则拒绝，force=True 强制删除。

    Args:
        asset_path: 要删除的资产路径
        force: 是否强制删除（忽略引用检查）
    """
    params: dict = {"asset_path": asset_path}
    if force:
        params["force"] = True
    return await connection.send_request("delete_asset", params)


@mcp.tool()
async def save_asset(asset_path: str = "", save_all: bool = False) -> dict:
    """保存资产。传 asset_path 保存单个，传 save_all=True 保存所有脏资产。

    Args:
        asset_path: 要保存的资产路径，可选
        save_all: 是否保存所有脏资产
    """
    params: dict = {}
    if asset_path:
        params["asset_path"] = asset_path
    if save_all:
        params["save_all"] = True
    return await connection.send_request("save_asset", params)


@mcp.tool()
async def create_folder(folder_path: str) -> dict:
    """在 Content Browser 中创建文件夹。

    Args:
        folder_path: 文件夹路径，如 /Game/Materials/NewFolder
    """
    return await connection.send_request("create_folder", {"folder_path": folder_path})
