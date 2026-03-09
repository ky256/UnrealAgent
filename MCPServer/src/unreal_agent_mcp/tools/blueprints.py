"""Blueprint editing tools — read/write blueprint graphs, nodes, variables, functions."""

from ..server import mcp, connection


@mcp.tool()
async def get_blueprint_overview(asset_path: str) -> dict:
    """获取蓝图概览：父类、图列表、变量、事件、接口、编译状态。

    用于快速了解蓝图结构。

    Args:
        asset_path: 蓝图资产路径，如 /Game/Blueprints/BP_Player
    """
    return await connection.send_request("get_blueprint_overview", {"asset_path": asset_path})


@mcp.tool()
async def get_blueprint_graph(asset_path: str, graph_name: str = "") -> dict:
    """获取蓝图节点图详情：所有节点、引脚、连接关系。

    类似 get_material_graph 的蓝图版本。

    Args:
        asset_path: 蓝图资产路径
        graph_name: 图名称，如 EventGraph。留空默认 EventGraph
    """
    params: dict = {"asset_path": asset_path}
    if graph_name:
        params["graph_name"] = graph_name
    return await connection.send_request("get_blueprint_graph", params)


@mcp.tool()
async def get_blueprint_variables(asset_path: str) -> dict:
    """获取蓝图的所有变量定义列表。

    返回名称、类型、默认值、是否公开、是否复制。

    Args:
        asset_path: 蓝图资产路径
    """
    return await connection.send_request("get_blueprint_variables", {"asset_path": asset_path})


@mcp.tool()
async def get_blueprint_functions(asset_path: str) -> dict:
    """获取蓝图自定义函数签名列表。

    返回函数名、输入输出参数、是否纯函数。

    Args:
        asset_path: 蓝图资产路径
    """
    return await connection.send_request("get_blueprint_functions", {"asset_path": asset_path})


@mcp.tool()
async def add_node(
    asset_path: str,
    node_class: str,
    graph_name: str = "",
    function_name: str = "",
    target_class: str = "",
    event_name: str = "",
    variable_name: str = "",
    macro_path: str = "",
    node_pos_x: int = 0,
    node_pos_y: int = 0,
) -> dict:
    """在蓝图图中添加节点。

    Args:
        asset_path: 蓝图资产路径
        node_class: 节点类型: CallFunction, Event, CustomEvent, IfThenElse,
                    VariableGet, VariableSet, MacroInstance
        graph_name: 图名称，留空默认 EventGraph
        function_name: CallFunction 时的函数名 (如 PrintString)
        target_class: CallFunction 时的目标类 (如 KismetSystemLibrary)
        event_name: Event/CustomEvent 的事件名
        variable_name: VariableGet/Set 的变量名
        macro_path: MacroInstance 的宏资产路径
        node_pos_x: 节点 X 位置
        node_pos_y: 节点 Y 位置
    """
    params: dict = {"asset_path": asset_path, "node_class": node_class}
    if graph_name:
        params["graph_name"] = graph_name
    if function_name:
        params["function_name"] = function_name
    if target_class:
        params["target_class"] = target_class
    if event_name:
        params["event_name"] = event_name
    if variable_name:
        params["variable_name"] = variable_name
    if macro_path:
        params["macro_path"] = macro_path
    if node_pos_x != 0:
        params["node_pos_x"] = node_pos_x
    if node_pos_y != 0:
        params["node_pos_y"] = node_pos_y
    return await connection.send_request("add_node", params)


@mcp.tool()
async def delete_node(
    asset_path: str,
    node_index: int,
    graph_name: str = "",
) -> dict:
    """删除蓝图图中指定索引的节点。

    Args:
        asset_path: 蓝图资产路径
        node_index: 要删除的节点索引（来自 get_blueprint_graph）
        graph_name: 图名称，留空默认 EventGraph
    """
    params: dict = {"asset_path": asset_path, "node_index": node_index}
    if graph_name:
        params["graph_name"] = graph_name
    return await connection.send_request("delete_node", params)


@mcp.tool()
async def connect_pins(
    asset_path: str,
    from_node_index: int,
    from_pin: str,
    to_node_index: int,
    to_pin: str,
    graph_name: str = "",
) -> dict:
    """连接蓝图中两个节点的引脚。

    Args:
        asset_path: 蓝图资产路径
        from_node_index: 源节点索引
        from_pin: 源节点输出引脚名称
        to_node_index: 目标节点索引
        to_pin: 目标节点输入引脚名称
        graph_name: 图名称，留空默认 EventGraph
    """
    params: dict = {
        "asset_path": asset_path,
        "from_node_index": from_node_index,
        "from_pin": from_pin,
        "to_node_index": to_node_index,
        "to_pin": to_pin,
    }
    if graph_name:
        params["graph_name"] = graph_name
    return await connection.send_request("connect_pins", params)


@mcp.tool()
async def disconnect_pin(
    asset_path: str,
    node_index: int,
    pin_name: str,
    graph_name: str = "",
) -> dict:
    """断开蓝图节点某个引脚的所有连接。

    Args:
        asset_path: 蓝图资产路径
        node_index: 节点索引
        pin_name: 要断开连接的引脚名称
        graph_name: 图名称，留空默认 EventGraph
    """
    params: dict = {"asset_path": asset_path, "node_index": node_index, "pin_name": pin_name}
    if graph_name:
        params["graph_name"] = graph_name
    return await connection.send_request("disconnect_pin", params)


@mcp.tool()
async def add_variable(
    asset_path: str,
    variable_name: str,
    variable_type: str,
    default_value: str = "",
    category: str = "",
    is_exposed: bool = False,
) -> dict:
    """向蓝图添加新变量。

    Args:
        asset_path: 蓝图资产路径
        variable_name: 变量名
        variable_type: 类型: bool, int, float, string, vector, rotator,
                       transform, text, name, object
        default_value: 默认值字符串，可选
        category: 变量分类，可选
        is_exposed: 是否公开为 Instance Editable
    """
    params: dict = {
        "asset_path": asset_path,
        "variable_name": variable_name,
        "variable_type": variable_type,
    }
    if default_value:
        params["default_value"] = default_value
    if category:
        params["category"] = category
    if is_exposed:
        params["is_exposed"] = True
    return await connection.send_request("add_variable", params)


@mcp.tool()
async def add_function(
    asset_path: str,
    function_name: str,
    is_pure: bool = False,
) -> dict:
    """向蓝图添加新的自定义函数。

    Args:
        asset_path: 蓝图资产路径
        function_name: 函数名
        is_pure: 是否为纯函数
    """
    params: dict = {"asset_path": asset_path, "function_name": function_name}
    if is_pure:
        params["is_pure"] = True
    return await connection.send_request("add_function", params)


@mcp.tool()
async def compile_blueprint(asset_path: str) -> dict:
    """编译蓝图并返回编译结果。

    返回状态（UpToDate/Error/Warning）、错误列表、警告列表。
    用于操作后的验证闭环。

    Args:
        asset_path: 蓝图资产路径
    """
    return await connection.send_request("compile_blueprint", {"asset_path": asset_path})
