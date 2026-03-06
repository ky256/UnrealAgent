"""材质编辑工具 — 通过 MCP 暴露材质图操作能力。"""

from ..server import mcp, connection


@mcp.tool()
async def get_material_graph(asset_path: str) -> dict:
    """获取完整的材质节点图：所有表达式节点、连接关系和材质属性。

    Args:
        asset_path: 材质资产路径，例如 /Game/Materials/M_Base
    """
    return await connection.send_request(
        "get_material_graph", {"asset_path": asset_path}
    )


@mcp.tool()
async def create_material_expression(
    asset_path: str,
    expression_class: str,
    node_pos_x: int = 0,
    node_pos_y: int = 0,
) -> dict:
    """在材质图中创建一个表达式节点。

    常用 expression_class:
    MaterialExpressionConstant, MaterialExpressionConstant3Vector,
    MaterialExpressionScalarParameter, MaterialExpressionVectorParameter,
    MaterialExpressionTextureSample, MaterialExpressionAdd,
    MaterialExpressionMultiply, MaterialExpressionLerp,
    MaterialExpressionCustom

    Args:
        asset_path: 材质资产路径
        expression_class: 表达式类名（不带 U 前缀）
        node_pos_x: 节点 X 坐标，默认 0
        node_pos_y: 节点 Y 坐标，默认 0
    """
    return await connection.send_request(
        "create_material_expression",
        {
            "asset_path": asset_path,
            "expression_class": expression_class,
            "node_pos_x": node_pos_x,
            "node_pos_y": node_pos_y,
        },
    )


@mcp.tool()
async def delete_material_expression(
    asset_path: str, expression_index: int
) -> dict:
    """删除材质图中指定索引的表达式节点（索引来自 get_material_graph）。

    Args:
        asset_path: 材质资产路径
        expression_index: 要删除的表达式索引
    """
    return await connection.send_request(
        "delete_material_expression",
        {"asset_path": asset_path, "expression_index": expression_index},
    )


@mcp.tool()
async def connect_material_property(
    asset_path: str,
    expression_index: int,
    property: str,
    output_name: str = "",
) -> dict:
    """将表达式输出连接到材质属性引脚。

    Args:
        asset_path: 材质资产路径
        expression_index: 源表达式索引
        property: 目标材质属性，可选：MP_BaseColor, MP_Metallic, MP_Specular,
                  MP_Roughness, MP_Normal, MP_EmissiveColor, MP_Opacity,
                  MP_OpacityMask, MP_AmbientOcclusion
        output_name: 输出引脚名称，留空使用默认输出
    """
    return await connection.send_request(
        "connect_material_property",
        {
            "asset_path": asset_path,
            "expression_index": expression_index,
            "output_name": output_name,
            "property": property,
        },
    )


@mcp.tool()
async def connect_material_expressions(
    asset_path: str,
    from_index: int,
    to_index: int,
    from_output: str = "",
    to_input: str = "",
) -> dict:
    """连接两个表达式节点：将一个节点的输出连接到另一个节点的输入。

    Args:
        asset_path: 材质资产路径
        from_index: 源表达式索引
        to_index: 目标表达式索引
        from_output: 源输出引脚名称，留空使用默认
        to_input: 目标输入引脚名称，留空使用默认
    """
    return await connection.send_request(
        "connect_material_expressions",
        {
            "asset_path": asset_path,
            "from_index": from_index,
            "from_output": from_output,
            "to_index": to_index,
            "to_input": to_input,
        },
    )


@mcp.tool()
async def set_expression_value(
    asset_path: str,
    expression_index: int,
    property_name: str,
    value: str,
) -> dict:
    """设置表达式节点的属性值。

    不同表达式类型对应的 property_name 和 value 格式：
    - Constant: property_name='R', value='0.5'
    - Constant3Vector: property_name='Constant', value='{"r":1,"g":0,"b":0,"a":1}'
    - ScalarParameter: property_name='DefaultValue', value='0.5'
    - VectorParameter: property_name='DefaultValue', value='{"r":1,"g":0,"b":0,"a":1}'
    - TextureSample: property_name='texture_path', value='/Game/Textures/T_MyTex'

    Args:
        asset_path: 材质资产路径
        expression_index: 表达式索引
        property_name: 属性名称
        value: JSON 格式的值字符串
    """
    return await connection.send_request(
        "set_expression_value",
        {
            "asset_path": asset_path,
            "expression_index": expression_index,
            "property_name": property_name,
            "value": value,
        },
    )


@mcp.tool()
async def recompile_material(asset_path: str) -> dict:
    """重编译材质。在修改材质图后调用此工具使更改生效。

    Args:
        asset_path: 材质资产路径
    """
    return await connection.send_request(
        "recompile_material", {"asset_path": asset_path}
    )


@mcp.tool()
async def layout_material_expressions(asset_path: str) -> dict:
    """自动排列材质图中的表达式节点为网格布局。

    Args:
        asset_path: 材质资产路径
    """
    return await connection.send_request(
        "layout_material_expressions", {"asset_path": asset_path}
    )


@mcp.tool()
async def get_material_parameters(asset_path: str) -> dict:
    """获取材质或材质实例的所有参数名称（scalar, vector, texture, static_switch）。

    Args:
        asset_path: 材质或材质实例资产路径
    """
    return await connection.send_request(
        "get_material_parameters", {"asset_path": asset_path}
    )


@mcp.tool()
async def set_material_instance_param(
    asset_path: str,
    param_name: str,
    param_type: str,
    value: str,
) -> dict:
    """设置 MaterialInstanceConstant 的参数值。

    Args:
        asset_path: 材质实例资产路径
        param_name: 参数名称
        param_type: 参数类型：scalar, vector, texture, static_switch
        value: 值字符串
    """
    return await connection.send_request(
        "set_material_instance_param",
        {
            "asset_path": asset_path,
            "param_name": param_name,
            "param_type": param_type,
            "value": value,
        },
    )


@mcp.tool()
async def set_material_property(
    asset_path: str,
    property_name: str,
    value: str,
) -> dict:
    """设置材质全局属性。

    支持的属性：BlendMode, ShadingModel, TwoSided, OpacityMaskClipValue

    Args:
        asset_path: 材质资产路径
        property_name: 属性名：BlendMode, ShadingModel, TwoSided, OpacityMaskClipValue
        value: 值字符串
    """
    return await connection.send_request(
        "set_material_property",
        {
            "asset_path": asset_path,
            "property_name": property_name,
            "value": value,
        },
    )
