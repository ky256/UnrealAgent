"""Universal property read/write tools — based on UE reflection system."""

from ..server import mcp, connection


@mcp.tool()
async def get_property(actor_name: str, property_path: str) -> dict:
    """读取 Actor/Component 的任意属性值。

    通过 UE 反射系统支持属性路径解析，可以读取任何可编辑属性。

    Args:
        actor_name: Actor 名称（label 或内部名）。
        property_path: 属性路径，格式: 'ComponentName.PropertyName' 或
                       'ComponentName.StructProp.Field'。
                       如果只提供属性名（无点号），则直接在 Actor 上查找。
                       示例: 'LightComponent.Intensity',
                             'StaticMeshComponent.StaticMesh',
                             'RootComponent.RelativeLocation.X'
    """
    return await connection.send_request(
        "get_property",
        {"actor_name": actor_name, "property_path": property_path},
    )


@mcp.tool()
async def set_property(actor_name: str, property_path: str, value: str | int | float | bool | dict) -> dict:
    """设置 Actor/Component 的任意属性值。支持 Undo。

    操作后会触发 PostEditChangeProperty 通知，保证编辑器 UI 同步更新。

    Args:
        actor_name: Actor 名称（label 或内部名）。
        property_path: 属性路径，与 get_property 格式相同。
        value: 要设置的值。数值传数字，字符串传字符串，布尔传 true/false，
               结构体（如 FVector）传 dict: {"x":1,"y":2,"z":3}，
               对象引用传资产路径字符串。
    """
    return await connection.send_request(
        "set_property",
        {"actor_name": actor_name, "property_path": property_path, "value": value},
    )


@mcp.tool()
async def list_properties(actor_name: str, component_name: str = "") -> dict:
    """列出 Actor 或组件的所有可编辑属性。

    返回名称、类型、当前值预览、是否可编辑。
    仅列出 EditAnywhere/VisibleAnywhere 标记的属性。

    Args:
        actor_name: Actor 名称（label 或内部名）。
        component_name: 组件名称。留空列出 Actor 自身的属性及其组件列表。
    """
    params: dict = {"actor_name": actor_name}
    if component_name:
        params["component_name"] = component_name
    return await connection.send_request("list_properties", params)
