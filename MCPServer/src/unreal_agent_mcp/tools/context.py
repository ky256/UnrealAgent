"""Editor context awareness tools — let Agent know what the user is doing."""

from ..server import mcp, connection


@mcp.tool()
async def get_open_editors() -> dict:
    """获取当前打开的所有资产编辑器列表。

    返回每个编辑器的 asset_path、asset_name、asset_class、editor_name。
    用于理解用户当前正在编辑什么资产（解决 "这个" 的指代消歧）。
    """
    return await connection.send_request("get_open_editors", {})


@mcp.tool()
async def get_selected_assets() -> dict:
    """获取 Content Browser 中当前选中的资产列表。

    返回 path、name、class。
    """
    return await connection.send_request("get_selected_assets", {})


@mcp.tool()
async def get_browser_path() -> dict:
    """获取 Content Browser 当前浏览的文件夹路径。"""
    return await connection.send_request("get_browser_path", {})


@mcp.tool()
async def get_message_log(
    category: str = "",
    count: int = 50,
    severity: str = "",
) -> dict:
    """获取最近的消息日志（包含编译错误、警告等）。

    可按类别和严重级别过滤。适合在编译/操作后检查是否有错误。

    Args:
        category: 日志类别过滤（如 BlueprintLog、PIE、MaterialEditor 等），留空返回所有。
        count: 返回的日志条数，默认 50，最大 200。
        severity: 严重级别过滤：Error, Warning, Log, Display 等。留空返回所有。
    """
    params: dict = {"count": min(max(count, 1), 200)}
    if category:
        params["category"] = category
    if severity:
        params["severity"] = severity
    return await connection.send_request("get_message_log", params)


@mcp.tool()
async def get_output_log(
    count: int = 50,
    filter: str = "",
) -> dict:
    """获取输出日志最近 N 条。可按关键字过滤。

    Args:
        count: 返回的日志条数，默认 50，最大 200。
        filter: 文本过滤关键字，只返回包含此关键字的日志行。
    """
    params: dict = {"count": min(max(count, 1), 200)}
    if filter:
        params["filter"] = filter
    return await connection.send_request("get_output_log", params)
