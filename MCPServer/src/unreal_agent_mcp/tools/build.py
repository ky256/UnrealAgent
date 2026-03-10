"""Build and launch Unreal Editor tools.

These tools work WITHOUT a running editor — they operate at the OS level,
calling UnrealBuildTool and spawning the editor process directly.
"""

import asyncio
import json
import logging
import os
import platform
import re
import subprocess
import time
from pathlib import Path

from ..server import mcp, connection

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# 全局编译状态（MCP Server 进程级单例）
# ---------------------------------------------------------------------------

_build_state: dict = {
    "status": "idle",           # idle / building / success / failed
    "progress_pct": 0,          # 编译进度百分比
    "current_target": "",       # 当前编译目标描述
    "start_time": None,         # 编译开始时间戳
    "elapsed_seconds": 0,       # 已用时间
    "output_lines": [],         # 最近 N 行编译输出（滚动窗口）
    "error_lines": [],          # 错误输出
    "return_code": None,        # 进程退出码
    "uproject_path": "",        # 编译的项目路径
}

_build_process: asyncio.subprocess.Process | None = None

# 编辑器启动状态（MCP Server 进程级单例）
_editor_state: dict = {
    "status": "idle",  # idle / launching / waiting_tcp / ready / timeout / error
    "uproject_path": "",
    "start_time": None,
    "elapsed_seconds": 0,
    "message": "",
    "project_info": None,
}

# 编译进度正则：匹配 [current/total] 格式
_PROGRESS_PATTERN = re.compile(r"\[(\d+)/(\d+)\]")


# ---------------------------------------------------------------------------
# 引擎路径定位
# ---------------------------------------------------------------------------

def _validate_engine_dir(path: str) -> bool:
    """验证引擎目录包含必要的可执行文件。"""
    engine_subdir = os.path.join(path, "Engine")
    # 有些安装方式引擎根目录本身包含 Engine 子目录，有些直接就是 Engine
    if os.path.isdir(engine_subdir):
        check_base = engine_subdir
    else:
        check_base = path

    return (
        os.path.isfile(os.path.join(check_base, "Build", "BatchFiles", "Build.bat"))
        or os.path.isfile(os.path.join(check_base, "Binaries", "Win64", "UnrealEditor.exe"))
    )


def _get_engine_subdir(engine_root: str) -> str:
    """返回实际的 Engine 子目录路径。"""
    engine_subdir = os.path.join(engine_root, "Engine")
    if os.path.isdir(engine_subdir):
        return engine_subdir
    return engine_root


def find_engine_path(uproject_path: str) -> str | None:
    """通过 .uproject 定位引擎安装路径。

    定位策略（按优先级）：
    1. 环境变量 UNREAL_ENGINE_PATH 直接指定
    2. .uproject → EngineAssociation → Windows 注册表
    3. 常见安装路径扫描
    """
    # 策略 1: 环境变量
    env_path = os.environ.get("UNREAL_ENGINE_PATH")
    if env_path and _validate_engine_dir(env_path):
        return env_path

    # 读取 .uproject 获取引擎版本
    engine_version = ""
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            uproject = json.load(f)
        engine_version = uproject.get("EngineAssociation", "")
    except (OSError, json.JSONDecodeError) as e:
        logger.warning(f"无法读取 .uproject 文件: {e}")

    # 策略 2: Windows 注册表查询
    if engine_version and platform.system() == "Windows":
        try:
            import winreg
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                rf"SOFTWARE\EpicGames\Unreal Engine\{engine_version}",
            )
            install_dir, _ = winreg.QueryValueEx(key, "InstalledDirectory")
            winreg.CloseKey(key)
            if install_dir and _validate_engine_dir(install_dir):
                return install_dir
        except (OSError, FileNotFoundError, ImportError):
            pass

    # 策略 3: 常见安装路径扫描
    if engine_version:
        for drive in "CDEFGHI":
            candidate = f"{drive}:/Program Files/Epic Games/UE_{engine_version}"
            if _validate_engine_dir(candidate):
                return candidate

    return None


def _extract_project_name(uproject_path: str) -> str:
    """从 .uproject 路径中提取项目名（不含扩展名）。"""
    return Path(uproject_path).stem


def _validate_uproject(uproject_path: str) -> str | None:
    """验证 .uproject 文件路径，返回错误消息或 None。"""
    if not uproject_path:
        return "未提供 .uproject 文件路径"
    if not os.path.isfile(uproject_path):
        return f".uproject 文件不存在: {uproject_path}"
    if not uproject_path.endswith(".uproject"):
        return f"文件不是 .uproject 格式: {uproject_path}"
    return None


# ---------------------------------------------------------------------------
# 编译进度解析
# ---------------------------------------------------------------------------

def _parse_progress(line: str) -> float | None:
    """从 UBT 输出行中提取编译进度百分比。"""
    match = _PROGRESS_PATTERN.search(line)
    if match:
        current, total = int(match.group(1)), int(match.group(2))
        if total > 0:
            return round((current / total) * 100, 1)
    return None


# ---------------------------------------------------------------------------
# 异步编译子进程
# ---------------------------------------------------------------------------

async def _run_build_process(cmd: list[str]) -> None:
    """在后台运行编译进程并持续更新全局状态。"""
    global _build_state, _build_process

    try:
        _build_process = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # 实时读取 stdout
        while True:
            line = await _build_process.stdout.readline()
            if not line:
                break
            text = line.decode("utf-8", errors="replace").strip()
            if text:
                _build_state["output_lines"].append(text)
                # 保持滚动窗口 100 行
                if len(_build_state["output_lines"]) > 100:
                    _build_state["output_lines"].pop(0)
                # 更新进度
                pct = _parse_progress(text)
                if pct is not None:
                    _build_state["progress_pct"] = pct
                    _build_state["current_target"] = text

        # 等待进程结束
        await _build_process.wait()
        _build_state["return_code"] = _build_process.returncode

        # 读取 stderr
        stderr_data = await _build_process.stderr.read()
        if stderr_data:
            _build_state["error_lines"] = (
                stderr_data.decode("utf-8", errors="replace")
                .strip()
                .split("\n")[-50:]
            )

        _build_state["status"] = (
            "success" if _build_process.returncode == 0 else "failed"
        )

    except Exception as e:
        logger.error(f"编译进程异常: {e}")
        _build_state["status"] = "failed"
        _build_state["error_lines"] = [str(e)]

    finally:
        if _build_state["start_time"]:
            _build_state["elapsed_seconds"] = round(
                time.time() - _build_state["start_time"], 1
            )
        _build_process = None


# ---------------------------------------------------------------------------
# MCP 工具定义
# ---------------------------------------------------------------------------

@mcp.tool()
async def build_project(
    uproject_path: str,
    configuration: str = "Development",
    platform_name: str = "Win64",
    clean: bool = False,
) -> dict:
    """编译 UE 项目（调用 UnrealBuildTool）。

    启动编译后立即返回，通过 get_build_status 轮询进度。
    不需要 UE 编辑器运行。

    Args:
        uproject_path: .uproject 文件的完整路径（如 I:/Aura/Aura.uproject）。
        configuration: 编译配置：Development（默认）、DebugGame、Shipping。
        platform_name: 目标平台，默认 Win64。
        clean: 是否执行 Clean Build。
    """
    global _build_state

    # 防止重复编译
    if _build_state["status"] == "building":
        return {
            "status": "already_building",
            "message": "编译正在进行中，请使用 get_build_status 查看进度",
            "progress_pct": _build_state["progress_pct"],
        }

    # 验证 .uproject
    err = _validate_uproject(uproject_path)
    if err:
        return {"error": err}

    # 定位引擎路径
    engine_path = find_engine_path(uproject_path)
    if not engine_path:
        return {
            "error": (
                "找不到 UE 引擎路径。请尝试以下方式之一：\n"
                "1. 设置环境变量 UNREAL_ENGINE_PATH 指向引擎根目录\n"
                "2. 确认 Epic Games Launcher 已正确安装引擎"
            )
        }

    engine_dir = _get_engine_subdir(engine_path)

    # 验证 Build.bat 存在
    build_bat = os.path.join(engine_dir, "Build", "BatchFiles", "Build.bat")
    if not os.path.isfile(build_bat):
        return {"error": f"找不到编译脚本: {build_bat}"}

    # 构造编译命令
    project_name = _extract_project_name(uproject_path)
    cmd = [
        build_bat,
        f"{project_name}Editor",
        platform_name,
        configuration,
        f"-Project={uproject_path}",
        "-WaitMutex",
        "-FromMsBuild",
    ]
    if clean:
        cmd.append("-Clean")

    logger.info(f"启动编译: {' '.join(cmd)}")

    # 重置编译状态
    _build_state = {
        "status": "building",
        "progress_pct": 0,
        "current_target": f"{project_name}Editor ({platform_name}/{configuration})",
        "start_time": time.time(),
        "elapsed_seconds": 0,
        "output_lines": [],
        "error_lines": [],
        "return_code": None,
        "uproject_path": uproject_path,
    }

    # 启动异步编译任务
    asyncio.create_task(_run_build_process(cmd))

    return {
        "status": "building",
        "message": f"编译已启动: {project_name}Editor ({platform_name}/{configuration})",
        "engine_path": engine_path,
    }


@mcp.tool()
async def get_build_status() -> dict:
    """查询当前编译状态和进度。

    返回编译状态（idle/building/success/failed）、进度百分比、
    最近输出和已用时间。
    同时返回编辑器启动状态（如果正在等待）。
    """
    state = dict(_build_state)

    # 实时更新已用时间
    if state["status"] == "building" and state["start_time"]:
        state["elapsed_seconds"] = round(time.time() - state["start_time"], 1)

    # 返回最近 20 行输出（避免 token 过长）
    state["recent_output"] = state.get("output_lines", [])[-20:]
    # 移除完整输出（太长）
    state.pop("output_lines", None)
    state.pop("start_time", None)

    # 附加编辑器启动状态
    editor = dict(_editor_state)
    if editor["status"] in ("launching", "waiting_tcp") and editor.get("start_time"):
        editor["elapsed_seconds"] = round(time.time() - editor["start_time"], 1)
    editor.pop("start_time", None)
    state["editor"] = editor

    return state


async def _wait_for_editor_tcp(
    timeout_seconds: float = 300.0,
    poll_interval: float = 3.0,
) -> None:
    """后台任务：等待编辑器 TCP 连接就绪，更新 _editor_state。"""
    global _editor_state

    _editor_state["status"] = "waiting_tcp"
    _editor_state["message"] = "编辑器进程已启动，等待 TCP 连接就绪..."
    start = time.time()

    while time.time() - start < timeout_seconds:
        await asyncio.sleep(poll_interval)
        _editor_state["elapsed_seconds"] = round(time.time() - _editor_state["start_time"], 1)
        try:
            result = await asyncio.wait_for(
                connection.send_request("get_project_info", {}),
                timeout=5.0,
            )
            elapsed = round(time.time() - start, 1)
            _editor_state.update({
                "status": "ready",
                "message": f"编辑器已就绪（等待 {elapsed}s）",
                "elapsed_seconds": round(time.time() - _editor_state["start_time"], 1),
                "project_info": result,
            })
            logger.info(f"编辑器 TCP 就绪，耗时 {elapsed}s")
            return
        except (ConnectionError, OSError, RuntimeError, asyncio.TimeoutError):
            continue

    _editor_state.update({
        "status": "timeout",
        "message": f"编辑器启动超时（{timeout_seconds}s），TCP 连接未就绪",
        "elapsed_seconds": round(time.time() - _editor_state["start_time"], 1),
    })
    logger.warning(f"编辑器 TCP 等待超时: {timeout_seconds}s")


def _spawn_editor_process(editor_exe: str, uproject_path: str) -> None:
    """启动编辑器进程（detached，不随 MCP Server 退出而终止）。"""
    if platform.system() == "Windows":
        CREATE_NEW_PROCESS_GROUP = 0x00000200
        DETACHED_PROCESS = 0x00000008
        subprocess.Popen(
            [editor_exe, uproject_path],
            creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            close_fds=True,
        )
    else:
        subprocess.Popen(
            [editor_exe, uproject_path],
            start_new_session=True,
            close_fds=True,
        )


@mcp.tool()
async def launch_editor(
    uproject_path: str,
    wait_for_connection: bool = True,
    timeout_seconds: float = 300.0,
) -> dict:
    """启动 UE 编辑器并加载项目。

    不需要 UE 编辑器已在运行。如果编辑器已在运行，会直接返回当前状态。

    Args:
        uproject_path: .uproject 文件的完整路径。
        wait_for_connection: 是否等待编辑器 TCP 连接就绪（默认 True）。
        timeout_seconds: 等待连接就绪的超时时间（默认 300 秒）。
    """
    global _editor_state

    # 如果已在等待中，直接返回当前状态
    if _editor_state["status"] in ("launching", "waiting_tcp"):
        if _editor_state["start_time"]:
            _editor_state["elapsed_seconds"] = round(
                time.time() - _editor_state["start_time"], 1
            )
        return dict(_editor_state)

    # 验证 .uproject
    err = _validate_uproject(uproject_path)
    if err:
        return {"error": err}

    # 检查编辑器是否已在运行（短超时，快速检测）
    try:
        result = await asyncio.wait_for(
            connection.send_request("get_project_info", {}),
            timeout=5.0,
        )
        _editor_state.update({
            "status": "ready",
            "message": "编辑器已在运行",
            "project_info": result,
        })
        return {
            "status": "already_running",
            "message": "编辑器已在运行",
            "project_info": result,
        }
    except (ConnectionError, OSError, RuntimeError, asyncio.TimeoutError):
        pass  # 编辑器未运行，继续

    # 定位引擎路径
    engine_path = find_engine_path(uproject_path)
    if not engine_path:
        return {"error": "找不到 UE 引擎路径，请设置 UNREAL_ENGINE_PATH 环境变量"}

    engine_dir = _get_engine_subdir(engine_path)

    # 验证编辑器可执行文件
    editor_exe = os.path.join(engine_dir, "Binaries", "Win64", "UnrealEditor.exe")
    if not os.path.isfile(editor_exe):
        return {"error": f"找不到编辑器可执行文件: {editor_exe}"}

    # 启动编辑器进程
    logger.info(f"启动编辑器: {editor_exe} {uproject_path}")
    try:
        _spawn_editor_process(editor_exe, uproject_path)
    except OSError as e:
        return {"error": f"启动编辑器失败: {e}"}

    # 重置编辑器状态
    _editor_state = {
        "status": "launching",
        "uproject_path": uproject_path,
        "start_time": time.time(),
        "elapsed_seconds": 0,
        "message": "编辑器进程已启动",
        "project_info": None,
    }

    if wait_for_connection:
        # 后台等待 TCP 连接就绪（非阻塞）
        asyncio.create_task(_wait_for_editor_tcp(timeout_seconds))
        _editor_state["message"] = (
            "编辑器进程已启动，正在后台等待 TCP 连接就绪。"
            "请通过 get_build_status 轮询状态。"
        )

    return dict(_editor_state)


async def _build_then_launch_task(
    uproject_path: str,
    timeout: float = 600.0,
) -> None:
    """后台任务：等待编译完成后自动启动编辑器。"""
    global _editor_state

    start = time.time()
    while time.time() - start < timeout:
        await asyncio.sleep(2.0)
        if _build_state["status"] in ("success", "failed"):
            break

    if _build_state["status"] == "failed":
        _editor_state.update({
            "status": "error",
            "message": "编译失败，编辑器未启动",
        })
        logger.warning("build_and_launch: 编译失败，跳过编辑器启动")
        return

    if _build_state["status"] != "success":
        _editor_state.update({
            "status": "error",
            "message": f"编译等待超时（{timeout}s），编辑器未启动",
        })
        logger.warning("build_and_launch: 编译等待超时")
        return

    # 编译成功，启动编辑器
    logger.info("build_and_launch: 编译成功，开始启动编辑器")
    await launch_editor(
        uproject_path=uproject_path,
        wait_for_connection=True,
    )


@mcp.tool()
async def build_and_launch(
    uproject_path: str,
    configuration: str = "Development",
    skip_build: bool = False,
) -> dict:
    """编译项目并启动编辑器（一条龙）。

    完整流程：编译 → 等待完成 → 启动编辑器 → 等待 TCP 就绪。
    全程非阻塞，立即返回，通过 get_build_status 轮询进度。
    不需要 UE 编辑器已在运行。

    Args:
        uproject_path: .uproject 文件的完整路径。
        configuration: 编译配置，默认 Development。
        skip_build: 跳过编译直接启动（如果已经编译过）。
    """
    # 验证 .uproject
    err = _validate_uproject(uproject_path)
    if err:
        return {"error": err}

    if skip_build:
        # 直接启动编辑器（非阻塞）
        launch_result = await launch_editor(
            uproject_path=uproject_path,
            wait_for_connection=True,
        )
        return {
            "status": launch_result.get("status", "launching"),
            "message": "跳过编译，编辑器已启动。请通过 get_build_status 轮询状态。",
            "build": {"skipped": True},
            "editor": launch_result,
        }

    # 先检查是否已在编译
    if _build_state["status"] == "building":
        return {
            "status": "already_building",
            "message": "编译正在进行中，请通过 get_build_status 查看进度",
        }

    # 启动编译（非阻塞）
    build_result = await build_project(
        uproject_path=uproject_path,
        configuration=configuration,
    )
    if "error" in build_result:
        return {"status": "build_failed", "error": build_result["error"]}

    # 后台任务：等待编译完成后自动启动编辑器
    asyncio.create_task(_build_then_launch_task(uproject_path))

    return {
        "status": "building",
        "message": (
            "编译已启动，编译完成后将自动启动编辑器。"
            "请通过 get_build_status 轮询编译和编辑器状态。"
        ),
        "build": {
            "status": "building",
            "target": _build_state.get("current_target", ""),
        },
        "editor": {"status": "pending", "message": "等待编译完成后启动"},
    }
