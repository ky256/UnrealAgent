"""
Step 0: 截图功能完整测试（9 用例）
========================================
目标: 建立视觉验证能力，作为后续所有测试的验证基础设施。

测试用例:
  Layer 1 (Smoke):
    SS-56: take_screenshot() 默认参数
    SS-57: get_asset_thumbnail() 已知资产

  Layer 2 (功能正确性):
    F-SS-01: take_screenshot(mode="scene", quality="low")   → 512x512
    F-SS-02: take_screenshot(mode="scene", quality="high")  → 1280x720
    F-SS-03: take_screenshot(mode="viewport")               → 非空
    F-SS-04: take_screenshot(width=800, height=600)          → 800x600
    F-SS-05: take_screenshot(format="jpg")                   → .jpg 扩展名
    F-SS-06: get_asset_thumbnail(size=128)                   → 128x128
    F-SS-07: get_asset_thumbnail(size=512)                   → 512x512

运行方式:
  python -X utf8 step0_screenshot_test.py

依赖:
  pip install Pillow   (用于图片内容自动验证)
"""

import asyncio
import base64
import json
import os
import sys
import time
from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path
from typing import Any

# ---- 可选依赖：Pillow（用于视觉验证） ----
try:
    from PIL import Image
    import struct
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("⚠️  Pillow 未安装，视觉内容验证将跳过。安装: pip install Pillow")


# ============================================================
#  图片内容自动验证
# ============================================================

class VisualVerifier:
    """分析截图内容，自动检测黑屏、纯色、异常图片。"""

    @staticmethod
    def analyze(file_path: str) -> dict:
        """
        分析图片文件，返回验证结果。

        返回字典:
          - valid: bool — 图片内容是否看起来正常
          - reason: str — 判断原因
          - is_black: bool — 是否全黑
          - is_solid_color: bool — 是否纯色
          - unique_colors: int — 唯一颜色数量（采样）
          - avg_brightness: float — 平均亮度 0-255
          - entropy: float — 信息熵（越高越丰富）
        """
        result = {
            "valid": False,
            "reason": "未分析",
            "is_black": False,
            "is_solid_color": False,
            "unique_colors": 0,
            "avg_brightness": 0.0,
            "entropy": 0.0,
        }

        if not HAS_PIL:
            result["reason"] = "Pillow 未安装，跳过视觉验证"
            result["valid"] = True  # 无法验证时假定通过
            return result

        if not os.path.exists(file_path):
            result["reason"] = f"文件不存在: {file_path}"
            return result

        try:
            img = Image.open(file_path)
            img = img.convert("RGB")
        except Exception as e:
            result["reason"] = f"无法打开图片: {e}"
            return result

        width, height = img.size
        if width == 0 or height == 0:
            result["reason"] = "图片尺寸为 0"
            return result

        # 采样分析（为性能考虑，缩小到 64x64 分析）
        thumb = img.resize((64, 64), Image.Resampling.NEAREST)
        pixels = list(thumb.getdata())
        total = len(pixels)

        # 唯一颜色数
        unique = len(set(pixels))
        result["unique_colors"] = unique

        # 平均亮度
        brightness = sum(0.299 * r + 0.587 * g + 0.114 * b for r, g, b in pixels) / total
        result["avg_brightness"] = round(brightness, 1)

        # 信息熵（基于灰度直方图）
        gray = thumb.convert("L")
        histogram = gray.histogram()
        total_pixels = sum(histogram)
        entropy = 0.0
        import math
        for count in histogram:
            if count > 0:
                p = count / total_pixels
                entropy -= p * math.log2(p)
        result["entropy"] = round(entropy, 2)

        # 判断：全黑
        if brightness < 3.0 and unique <= 5:
            result["is_black"] = True
            result["reason"] = f"疑似黑屏: 亮度={brightness:.1f}, 唯一色={unique}"
            return result

        # 判断：纯色
        if unique <= 3:
            result["is_solid_color"] = True
            result["reason"] = f"疑似纯色: 唯一色={unique}, 主色={pixels[0]}"
            return result

        # 判断：信息量极低（可能渲染失败）
        if entropy < 0.5 and unique < 10:
            result["reason"] = f"信息量极低: 熵={entropy:.2f}, 唯一色={unique}"
            return result

        # 通过所有检查
        result["valid"] = True
        result["reason"] = f"正常: 唯一色={unique}, 亮度={brightness:.1f}, 熵={entropy:.2f}"
        return result

    @staticmethod
    def image_to_base64_thumbnail(file_path: str, max_size: int = 200) -> str:
        """将图片转换为 base64 编码的缩略图（用于 HTML 报告内嵌）。"""
        if not HAS_PIL or not os.path.exists(file_path):
            return ""
        try:
            img = Image.open(file_path)
            img.thumbnail((max_size, max_size), Image.Resampling.LANCZOS)
            buf = BytesIO()
            img.save(buf, format="PNG")
            return base64.b64encode(buf.getvalue()).decode("ascii")
        except Exception:
            return ""


# ============================================================
#  TCP JSON-RPC 客户端（精简版，复刻 connection.py 协议）
# ============================================================

class UnrealRPCClient:
    """直接通过 TCP JSON-RPC 与 UnrealAgent 插件通信的轻量客户端。"""

    def __init__(self, host: str = "127.0.0.1", port: int = 55557):
        self.host = host
        self.port = port
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self._request_id = 0

    async def connect(self) -> bool:
        try:
            self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
            print(f"  ✅ 已连接到 UnrealAgent @ {self.host}:{self.port}")
            return True
        except (ConnectionRefusedError, OSError) as e:
            print(f"  ❌ 无法连接到 {self.host}:{self.port}: {e}")
            return False

    async def disconnect(self):
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None
            self.reader = None

    async def call(self, method: str, params: dict | None = None) -> dict:
        """发送 JSON-RPC 请求并返回完整 response（含 result 或 error）。"""
        if not self.writer or self.writer.is_closing():
            if not await self.connect():
                return {"error": {"code": -1, "message": "Connection failed"}}

        self._request_id += 1
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self._request_id,
        }

        payload = json.dumps(request).encode("utf-8")
        header = f"Content-Length: {len(payload)}\r\n\r\n".encode("utf-8")

        try:
            self.writer.write(header + payload)
            await self.writer.drain()
        except (ConnectionError, OSError) as e:
            await self.disconnect()
            return {"error": {"code": -2, "message": f"Send failed: {e}"}}

        try:
            response_data = await self._read_response()
            return json.loads(response_data)
        except Exception as e:
            await self.disconnect()
            return {"error": {"code": -3, "message": f"Read failed: {e}"}}

    async def _read_response(self) -> bytes:
        content_length = None
        while True:
            line = await self.reader.readline()
            if not line:
                raise ConnectionError("Connection closed by server")
            line_str = line.decode("utf-8").strip()
            if line_str == "":
                if content_length is not None:
                    break
                continue
            if line_str.lower().startswith("content-length:"):
                content_length = int(line_str.split(":")[1].strip())
        if content_length is None:
            raise RuntimeError("No Content-Length header in response")
        return await self.reader.readexactly(content_length)


# ============================================================
#  测试结果记录
# ============================================================

@dataclass
class TestResult:
    test_id: str
    description: str
    passed: bool = False
    actual: str = ""
    expected: str = ""
    error: str = ""
    duration_ms: float = 0.0
    details: dict = field(default_factory=dict)
    image_path: str = ""           # 截图文件的本地路径
    visual_check: dict = field(default_factory=dict)  # VisualVerifier 的分析结果


class TestReport:
    def __init__(self):
        self.results: list[TestResult] = []

    def add(self, result: TestResult):
        self.results.append(result)
        status = "✅ PASS" if result.passed else "❌ FAIL"
        print(f"  {status}  [{result.test_id}] {result.description} ({result.duration_ms:.0f}ms)")
        if not result.passed and result.error:
            print(f"         错误: {result.error}")
        if result.details:
            for k, v in result.details.items():
                print(f"         {k}: {v}")
        # 打印视觉验证结果
        if result.visual_check:
            vc = result.visual_check
            icon = "🟢" if vc.get("valid") else "🔴"
            print(f"         视觉验证: {icon} {vc.get('reason', 'N/A')}")

    def summary(self):
        total = len(self.results)
        passed = sum(1 for r in self.results if r.passed)
        failed = total - passed
        visual_ok = sum(1 for r in self.results if r.visual_check.get("valid", False))
        print("\n" + "=" * 60)
        print(f"  Step 0 截图功能测试总结")
        print(f"  自动化验证: {passed}/{total} 通过    失败: {failed}/{total}")
        print(f"  视觉内容验证: {visual_ok}/{total} 通过")
        print("=" * 60)
        if failed > 0:
            print("\n  失败用例:")
            for r in self.results:
                if not r.passed:
                    print(f"    [{r.test_id}] {r.description}")
                    print(f"      期望: {r.expected}")
                    print(f"      实际: {r.actual}")
                    if r.error:
                        print(f"      错误: {r.error}")
        print()
        return passed, total


# ============================================================
#  HTML 可视化测试报告生成
# ============================================================

def generate_html_report(report: TestReport, output_path: str):
    """生成包含内嵌截图缩略图的 HTML 测试报告，供人类快速审阅。"""
    total = len(report.results)
    passed = sum(1 for r in report.results if r.passed)
    visual_ok = sum(1 for r in report.results if r.visual_check.get("valid", False))

    rows_html = ""
    for r in report.results:
        # 生成缩略图的 base64（如果有图片文件）
        thumb_b64 = ""
        if r.image_path and os.path.exists(r.image_path):
            thumb_b64 = VisualVerifier.image_to_base64_thumbnail(r.image_path, 150)

        thumb_html = (
            f'<img src="data:image/png;base64,{thumb_b64}" style="max-width:150px;max-height:150px;border:1px solid #ccc;cursor:pointer;" '
            f'title="点击查看原图" onclick="window.open(\'{r.image_path.replace(chr(92), "/")}\')"/>'
            if thumb_b64 else '<span style="color:#999;">无图</span>'
        )

        status_html = (
            '<span style="color:#2ecc71;font-weight:bold;">✅ PASS</span>'
            if r.passed else
            '<span style="color:#e74c3c;font-weight:bold;">❌ FAIL</span>'
        )

        vc = r.visual_check
        visual_html = ""
        if vc:
            v_icon = "🟢" if vc.get("valid") else "🔴"
            visual_html = f'{v_icon} {vc.get("reason", "N/A")}'
            if not vc.get("valid"):
                visual_html = f'<span style="color:#e74c3c;">{visual_html}</span>'
        else:
            visual_html = '<span style="color:#999;">—</span>'

        details_html = ""
        if r.details:
            details_html = "<br>".join(f"<small>{k}: {v}</small>" for k, v in r.details.items())

        rows_html += f"""
        <tr>
            <td><code>{r.test_id}</code></td>
            <td>{r.description}</td>
            <td>{status_html}</td>
            <td>{r.duration_ms:.0f}ms</td>
            <td>{thumb_html}</td>
            <td>{visual_html}</td>
            <td>{details_html}</td>
        </tr>"""

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>Step 0 截图功能测试报告</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; margin: 20px; background: #f8f9fa; }}
        h1 {{ color: #2c3e50; }}
        .summary {{ background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }}
        .summary .stat {{ display: inline-block; margin-right: 30px; }}
        .summary .stat .num {{ font-size: 28px; font-weight: bold; }}
        .summary .stat .label {{ color: #7f8c8d; font-size: 14px; }}
        .pass {{ color: #2ecc71; }}
        .fail {{ color: #e74c3c; }}
        table {{ width: 100%; border-collapse: collapse; background: white; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        th {{ background: #34495e; color: white; padding: 12px 10px; text-align: left; font-weight: 500; }}
        td {{ padding: 10px; border-bottom: 1px solid #ecf0f1; vertical-align: middle; }}
        tr:hover {{ background: #f1f8ff; }}
        code {{ background: #ecf0f1; padding: 2px 6px; border-radius: 3px; font-size: 12px; }}
        .note {{ background: #fff3cd; border: 1px solid #ffc107; padding: 15px; border-radius: 8px; margin-top: 20px; }}
        img {{ border-radius: 4px; }}
    </style>
</head>
<body>
    <h1>🎯 Step 0: 截图功能测试报告</h1>
    <p style="color:#7f8c8d;">生成时间: {time.strftime("%Y-%m-%d %H:%M:%S")}</p>

    <div class="summary">
        <div class="stat">
            <div class="num {'pass' if passed == total else 'fail'}">{passed}/{total}</div>
            <div class="label">自动化验证</div>
        </div>
        <div class="stat">
            <div class="num {'pass' if visual_ok == total else 'fail'}">{visual_ok}/{total}</div>
            <div class="label">视觉内容验证</div>
        </div>
        <div class="stat">
            <div class="num">{sum(r.duration_ms for r in report.results):.0f}ms</div>
            <div class="label">总耗时</div>
        </div>
    </div>

    <table>
        <thead>
            <tr>
                <th>用例ID</th>
                <th>描述</th>
                <th>结果</th>
                <th>耗时</th>
                <th>截图预览</th>
                <th>视觉验证</th>
                <th>详情</th>
            </tr>
        </thead>
        <tbody>
            {rows_html}
        </tbody>
    </table>

    <div class="note">
        <strong>🔍 人工审阅指引：</strong><br>
        请检查上方截图预览列中的图片：<br>
        1. <strong>scene 模式</strong>（SS-56, F-SS-01/02/04/05）：应显示 3D 场景内容，无编辑器 UI 叠加<br>
        2. <strong>viewport 模式</strong>（F-SS-03）：应显示编辑器视口，可能包含 gizmo/网格线<br>
        3. <strong>缩略图</strong>（SS-57, F-SS-06/07）：应显示材质球预览，大小应有明显差异<br>
        4. 所有图片应<strong>非黑屏、非纯色</strong>，内容清晰可辨<br>
        <br>
        点击缩略图可查看原始文件路径。
    </div>
</body>
</html>"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"  📊 HTML 可视化报告已生成: {output_path}")


# ============================================================
#  测试用例
# ============================================================

async def find_known_material(client: UnrealRPCClient) -> str | None:
    """查找项目中一个已知的材质资产路径，用于缩略图测试。"""
    resp = await client.call("search_assets", {"query": "M_", "class_filter": "Material"})
    if "result" in resp:
        assets = resp["result"].get("assets", [])
        if assets:
            path = assets[0].get("path", "") or assets[0].get("package", "")
            if "." in path.split("/")[-1]:
                path = path.rsplit(".", 1)[0]
            return path
    return None


async def run_test(
    client: UnrealRPCClient,
    report: TestReport,
    test_id: str,
    description: str,
    method: str,
    params: dict,
    expected: str,
    validator: callable,
):
    """运行单个测试用例。"""
    start = time.perf_counter()
    resp = await client.call(method, params)
    elapsed_ms = (time.perf_counter() - start) * 1000

    result = TestResult(
        test_id=test_id,
        description=description,
        expected=expected,
        duration_ms=elapsed_ms,
    )

    if "error" in resp:
        err = resp["error"]
        result.error = f"[{err.get('code', '?')}] {err.get('message', 'Unknown')}"
        result.actual = "RPC Error"
        result.passed = False
    else:
        data = resp.get("result", {})
        try:
            result.passed, result.actual, result.details = validator(data)
            # 提取图片路径用于视觉验证
            file_path = data.get("file_path", "")
            if file_path:
                result.image_path = file_path
        except Exception as e:
            result.error = f"Validator exception: {e}"
            result.actual = str(data)[:200]
            result.passed = False

    # 如果有图片文件，执行自动视觉内容验证
    if result.image_path:
        result.visual_check = VisualVerifier.analyze(result.image_path)
        # 视觉验证失败时降级为警告（不影响功能测试通过/失败）
        if not result.visual_check.get("valid", False) and result.passed:
            result.details["⚠️ 视觉警告"] = result.visual_check.get("reason", "")

    report.add(result)
    return result


# ============================================================
#  验证器（Validators）
# ============================================================

def validate_screenshot_basic(data: dict) -> tuple[bool, str, dict]:
    """基础截图验证：文件路径存在，尺寸字段存在。"""
    file_path = data.get("file_path", "")
    width = data.get("width", 0)
    height = data.get("height", 0)
    file_size = data.get("file_size_bytes", 0)
    success = data.get("success", False)

    details = {
        "file_path": file_path,
        "尺寸": f"{width}x{height}",
        "文件大小": f"{file_size} bytes",
    }

    file_exists = False
    if file_path and os.path.exists(file_path):
        file_exists = True
        actual_size = os.path.getsize(file_path)
        details["磁盘文件大小"] = f"{actual_size} bytes"

    passed = (
        success
        and bool(file_path)
        and width > 0
        and height > 0
        and file_size > 0
    )
    actual = f"success={success}, path={'存在' if file_exists else file_path}, {width}x{height}, {file_size}B"
    return passed, actual, details


def make_resolution_validator(expected_w: int, expected_h: int):
    """创建验证特定分辨率的验证器。"""
    def validator(data: dict) -> tuple[bool, str, dict]:
        passed_basic, actual, details = validate_screenshot_basic(data)
        width = data.get("width", 0)
        height = data.get("height", 0)
        resolution_match = (width == expected_w and height == expected_h)
        details["分辨率匹配"] = f"{'✅' if resolution_match else '❌'} 期望 {expected_w}x{expected_h}, 实际 {width}x{height}"
        passed = passed_basic and resolution_match
        return passed, actual, details
    return validator


def validate_screenshot_viewport(data: dict) -> tuple[bool, str, dict]:
    """Viewport 截图验证：不检查特定分辨率，只确认非空。"""
    passed, actual, details = validate_screenshot_basic(data)
    details["模式"] = "viewport"
    return passed, actual, details


def validate_screenshot_jpg(data: dict) -> tuple[bool, str, dict]:
    """JPG 格式验证。"""
    passed, actual, details = validate_screenshot_basic(data)
    file_path = data.get("file_path", "")
    is_jpg = file_path.lower().endswith(".jpg")
    details["格式验证"] = f"{'✅' if is_jpg else '❌'} 扩展名: {Path(file_path).suffix if file_path else 'N/A'}"
    passed = passed and is_jpg
    return passed, actual, details


def make_thumbnail_validator(expected_size: int):
    """创建缩略图尺寸验证器（精确匹配）。"""
    def validator(data: dict) -> tuple[bool, str, dict]:
        file_path = data.get("file_path", "")
        width = data.get("width", 0)
        height = data.get("height", 0)
        file_size = data.get("file_size_bytes", 0)
        success = data.get("success", False)

        details = {
            "file_path": file_path,
            "尺寸": f"{width}x{height}",
            "文件大小": f"{file_size} bytes",
        }

        # 精确检查：宽度和高度都应等于请求的 size
        size_match = (width == expected_size and height == expected_size)
        details["尺寸匹配"] = f"{'✅' if size_match else '❌'} 期望 {expected_size}x{expected_size}, 实际 {width}x{height}"

        passed = success and bool(file_path) and file_size > 0 and size_match
        actual = f"success={success}, {width}x{height}, {file_size}B"
        return passed, actual, details
    return validator


# ============================================================
#  主测试流程
# ============================================================

async def main():
    print("=" * 60)
    print("  Step 0: 截图功能完整测试（9 用例）")
    print("  目标: 建立视觉验证能力")
    print(f"  Pillow: {'✅ 已安装' if HAS_PIL else '❌ 未安装（视觉验证将跳过）'}")
    print("=" * 60)

    client = UnrealRPCClient()
    report = TestReport()

    # ---- 连接测试 ----
    print("\n📡 连接到 UnrealAgent...")
    if not await client.connect():
        print("\n❌ 无法连接到 UE 编辑器。请确保:")
        print("   1. UE 编辑器已启动")
        print("   2. UnrealAgent 插件已启用")
        print("   3. TCP Server 已在 127.0.0.1:55557 监听")
        return

    # ---- 查找测试资产 ----
    print("\n🔍 查找测试用材质资产...")
    material_path = await find_known_material(client)
    if material_path:
        print(f"  找到材质: {material_path}")
    else:
        print("  ⚠️  未找到材质资产，缩略图测试将使用回退路径")
        material_path = "/Game/StarterContent/Materials/M_Basic_Floor"

    # ============================================================
    #  Layer 1: Smoke Test（连通性）
    # ============================================================
    print("\n" + "-" * 40)
    print("  Layer 1: Smoke Test（连通性）")
    print("-" * 40)

    await run_test(
        client, report,
        test_id="SS-56",
        description="take_screenshot() 默认参数 (scene/high)",
        method="take_screenshot",
        params={},
        expected="file_path 存在, success=true",
        validator=validate_screenshot_basic,
    )

    await run_test(
        client, report,
        test_id="SS-57",
        description=f"get_asset_thumbnail({material_path})",
        method="get_asset_thumbnail",
        params={"asset_path": material_path},
        expected="file_path 存在, success=true, 256x256",
        validator=make_thumbnail_validator(256),
    )

    # ============================================================
    #  Layer 2: 功能正确性
    # ============================================================
    print("\n" + "-" * 40)
    print("  Layer 2: 功能正确性")
    print("-" * 40)

    await run_test(
        client, report,
        test_id="F-SS-01",
        description="take_screenshot(mode=scene, quality=low) → 512x512",
        method="take_screenshot",
        params={"mode": "scene", "quality": "low"},
        expected="512x512 PNG",
        validator=make_resolution_validator(512, 512),
    )

    await run_test(
        client, report,
        test_id="F-SS-02",
        description="take_screenshot(mode=scene, quality=high) → 1280x720",
        method="take_screenshot",
        params={"mode": "scene", "quality": "high"},
        expected="1280x720 PNG",
        validator=make_resolution_validator(1280, 720),
    )

    await run_test(
        client, report,
        test_id="F-SS-03",
        description="take_screenshot(mode=viewport) → 非空文件",
        method="take_screenshot",
        params={"mode": "viewport"},
        expected="文件存在，尺寸 > 0",
        validator=validate_screenshot_viewport,
    )

    await run_test(
        client, report,
        test_id="F-SS-04",
        description="take_screenshot(width=800, height=600) → 800x600",
        method="take_screenshot",
        params={"width": 800, "height": 600},
        expected="800x600 PNG",
        validator=make_resolution_validator(800, 600),
    )

    await run_test(
        client, report,
        test_id="F-SS-05",
        description="take_screenshot(format=jpg) → .jpg 文件",
        method="take_screenshot",
        params={"format": "jpg"},
        expected=".jpg 扩展名",
        validator=validate_screenshot_jpg,
    )

    await run_test(
        client, report,
        test_id="F-SS-06",
        description=f"get_asset_thumbnail(size=128) → 128x128",
        method="get_asset_thumbnail",
        params={"asset_path": material_path, "size": 128},
        expected="128x128 PNG",
        validator=make_thumbnail_validator(128),
    )

    await run_test(
        client, report,
        test_id="F-SS-07",
        description=f"get_asset_thumbnail(size=512) → 512x512",
        method="get_asset_thumbnail",
        params={"asset_path": material_path, "size": 512},
        expected="512x512 PNG",
        validator=make_thumbnail_validator(512),
    )

    # ============================================================
    #  测试总结
    # ============================================================
    passed, total = report.summary()

    # 保存 JSON 测试报告
    report_dir = Path(__file__).parent
    report_path = report_dir / "step0_results.json"
    report_data = {
        "test_name": "Step 0: Screenshot Functionality",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "passed": passed,
        "total": total,
        "results": [
            {
                "test_id": r.test_id,
                "description": r.description,
                "passed": r.passed,
                "expected": r.expected,
                "actual": r.actual,
                "error": r.error,
                "duration_ms": r.duration_ms,
                "details": r.details,
                "image_path": r.image_path,
                "visual_check": r.visual_check,
            }
            for r in report.results
        ],
    }
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report_data, f, ensure_ascii=False, indent=2)
    print(f"  📄 JSON 报告已保存: {report_path}")

    # 生成 HTML 可视化报告
    html_path = report_dir / "step0_report.html"
    generate_html_report(report, str(html_path))

    # 最终提示
    if passed == total:
        print("\n  🎉 所有自动化验证通过！")
        print(f"  📊 请在浏览器中打开 HTML 报告进行人工目视确认:")
        print(f"     {html_path}")
    else:
        print(f"\n  ⚠️  有 {total - passed} 个用例失败，请检查后再进行下一步。")

    await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
