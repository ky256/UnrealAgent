"""Editor event query tools — access cached editor events."""

from ..server import mcp, connection


@mcp.tool()
async def get_recent_events(count: int = 20, type_filter: str = "") -> dict:
    """获取最近的编辑器事件。

    事件类型:
    - SelectionChanged: Actor 选择变更
    - AssetEditorOpened: 打开了资产编辑器
    - AssetEditorClosed: 关闭了资产编辑器
    - PIEStarted: PIE 开始
    - PIEStopped: PIE 结束
    - AssetSaved: 资产保存
    - LevelChanged: 关卡/地图变更
    - UndoRedo: 撤销/重做

    Args:
        count: 返回事件数量，默认 20，最大 200
        type_filter: 按事件类型过滤，可选
    """
    params: dict = {}
    if count != 20:
        params["count"] = count
    if type_filter:
        params["type_filter"] = type_filter
    return await connection.send_request("get_recent_events", params)


@mcp.tool()
async def get_events_since(since: str, type_filter: str = "") -> dict:
    """获取指定时间之后的编辑器事件。

    Args:
        since: ISO 8601 时间字符串 (如 2026-03-09T10:00:00)
        type_filter: 按事件类型过滤，可选
    """
    params: dict = {"since": since}
    if type_filter:
        params["type_filter"] = type_filter
    return await connection.send_request("get_events_since", params)
