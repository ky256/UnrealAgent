"""Screenshot and visual inspection tools."""

import io
import os

from PIL import Image as PILImage
from mcp.server.fastmcp import Image as MCPImage

from ..server import mcp, connection


@mcp.tool()
async def take_screenshot(
    mode: str = "scene",
    quality: str = "high",
    width: int = 0,
    height: int = 0,
    format: str = "png",
    target: str = "active_window",
    asset_path: str = "",
    tab_id: str = "",
) -> dict:
    """截取编辑器截图。

    三种模式:
    - scene: 视口最终渲染画面（不含gizmo，推荐）
    - viewport: 回读当前编辑器视口像素（含 gizmo 等编辑器覆盖物）
    - editor: 截取编辑器UI面板截图（面板级精准截图）

    分辨率预设（quality，仅scene/viewport模式生效）:
    - low: 512x512
    - medium: 1024x1024
    - high: 1280x720（默认）
    - ultra: 1920x1080

    Args:
        mode: 截图模式 scene 或 viewport 或 editor
        quality: 分辨率预设
        width: 自定义宽度（覆盖 quality）
        height: 自定义高度（覆盖 quality）
        format: 输出格式 png 或 jpg
        target: editor模式专用，指定截图目标:
            - active_window: 当前活跃窗口（默认）
            - asset_editor: 资产编辑器内容区域（材质节点图/蓝图图等，不含Tab标签栏）
            - tab: 按TabID截取指定面板（需配合tab_id参数）
            - full: UE编辑器主窗口
        asset_path: target=asset_editor时使用，指定要截图的资产路径。不传则截取最近活跃的编辑器
        tab_id: target=tab时使用，面板的TabID。常用值:
            - LevelEditorSelectionDetails: Details面板
            - LevelEditorSceneOutliner: Outliner
            - ContentBrowserTab1: 内容浏览器
            - OutputLog: 输出日志
            - WorldSettings: 世界设置
    """
    params: dict = {}
    if mode != "scene":
        params["mode"] = mode
    if quality != "high":
        params["quality"] = quality
    if width > 0:
        params["width"] = width
    if height > 0:
        params["height"] = height
    if format != "png":
        params["format"] = format
    if target != "active_window":
        params["target"] = target
    if asset_path:
        params["asset_path"] = asset_path
    if tab_id:
        params["tab_id"] = tab_id
    return await connection.send_request("take_screenshot", params)


@mcp.tool()
async def get_asset_thumbnail(asset_path: str, size: int = 256) -> dict:
    """获取资产缩略图并保存为图片文件。

    Args:
        asset_path: 资产路径
        size: 缩略图尺寸，默认 256
    """
    params: dict = {"asset_path": asset_path}
    if size != 256:
        params["size"] = size
    return await connection.send_request("get_asset_thumbnail", params)


@mcp.tool()
async def read_image(
    file_path: str,
    max_dimension: int = 800,
) -> MCPImage:
    """读取图片文件并返回图像内容，供 AI 视觉模块直接查看分析。

    自动缩放大图以控制数据量。常用于查看 take_screenshot 或
    get_asset_thumbnail 保存的截图文件。

    Args:
        file_path: 图片文件的绝对路径（支持 png、jpg、bmp 等常见格式）
        max_dimension: 最大边长（像素），超过此值会等比缩放。默认 800。
                       设为 0 则不缩放，返回原始尺寸。
    """
    if not os.path.isfile(file_path):
        raise FileNotFoundError(f"图片文件不存在: {file_path}")

    img = PILImage.open(file_path)

    # 按需缩放
    if max_dimension > 0:
        w, h = img.size
        max_side = max(w, h)
        if max_side > max_dimension:
            scale = max_dimension / max_side
            new_w = int(w * scale)
            new_h = int(h * scale)
            img = img.resize((new_w, new_h), PILImage.LANCZOS)

    # 使用 JPEG 格式输出以减小 base64 数据量
    # 相同分辨率下 JPEG 比 PNG 小 50-70%，可支持更高分辨率
    buf = io.BytesIO()
    rgb = img.convert("RGB") if img.mode != "RGB" else img
    rgb.save(buf, format="JPEG", quality=85)
    image_data = buf.getvalue()

    return MCPImage(data=image_data, format="jpeg")