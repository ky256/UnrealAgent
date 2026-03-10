# 设计文档：编译项目 & 启动编辑器（Build & Launch）

## 从第一性原理出发

### 需求来源

用户反馈 UnrealAgent 当前的使用门槛高：必须先手动编译项目、手动启动 UE 编辑器、等待插件 TCP 服务就绪，才能开始使用 AI 工具。理想状态是 **一条指令完成从编译到就绪的全流程**。

### 核心问题

> 在 UE 编辑器尚未运行时，MCP 工具如何执行操作？

### 原子分解

把"编译并启动编辑器"拆到不可再分：

1. **定位引擎路径** — 知道 UBT 和 UnrealEditor.exe 在哪
2. **编译项目** — 调用 UnrealBuildTool 编译 C++ 代码
3. **启动编辑器** — spawn UnrealEditor.exe 进程并加载 .uproject
4. **等待就绪** — TCP :55557 端口可用，插件初始化完成

### 关键洞察

当前所有 MCP 工具都通过 `connection.send_request()` 与 UE 编辑器内 TCP 服务通信，**前提是编辑器已在运行**。但"编译并启动"操作发生在编辑器运行之前。

> UBT 和 UE Editor 都是本地进程 → 可通过 `subprocess` 直接启动  
> MCP Server 是 Python 进程 → 天然具备调用本地命令行的能力  
> **这个功能不需要 TCP connection，完全在 MCP Python 层实现**

这与其他工具有本质区别——它绕开了 C++ 插件层，直接操作操作系统。

---

## 架构定位

```
┌──────────────────────────────────────────────────────┐
│                MCP Tool Layer (Python)                │
├───────────────────┬──────────────────────────────────┤
│ 现有工具 (N 个)    │    build.py (新增)               │
│ 需要 UE 运行      │    不需要 UE 运行                 │
│ 走 TCP connection │    走 subprocess                  │
├───────────────────┴──────────────────────────────────┤
│            操作系统 / UE Engine 本地文件               │
└──────────────────────────────────────────────────────┘
```

与 `design-universal-execution.md` 中描述的三层架构（反射层 / 习得层 / 认知层）不同，build 工具属于**基础设施层**——它负责创建其他所有工具运行的前提条件。

---

## 新增工具列表

| 工具名 | 功能 | 需要 UE 运行 | 说明 |
|--------|------|:----------:|------|
| `build_project` | 编译 UE 项目 | ❌ | 调用 UBT，异步+进度追踪 |
| `launch_editor` | 启动 UE 编辑器 | ❌ | spawn 进程，可选等待 TCP 就绪 |
| `build_and_launch` | 编译 + 启动一条龙 | ❌ | 组合前两者 |
| `get_build_status` | 查询编译状态/进度 | ❌ | 轮询编译子进程输出 |

---

## 详细设计

### 1. 引擎路径定位

`.uproject` 文件中 `EngineAssociation` 字段标记引擎版本（如 `"5.7"`），需要通过该信息反查引擎安装路径。

#### 定位策略（按优先级）

```
1. 环境变量 UNREAL_ENGINE_PATH
   └─ 用户显式指定，最高优先级

2. .uproject → EngineAssociation → Windows 注册表
   └─ HKLM\SOFTWARE\EpicGames\Unreal Engine\{version}\InstalledDirectory
   └─ 这是 Epic Games Launcher 的标准注册方式

3. 常见安装路径扫描
   └─ C:/Program Files/Epic Games/UE_{version}/
   └─ D:/Program Files/Epic Games/UE_{version}/
   └─ 可扩展其他盘符

4. 从 MCP Server 自身路径推断
   └─ 如果 MCP Server 位于 {Project}/Plugins/UnrealAgent/MCPServer/
   └─ 尝试从项目路径向上查找引擎
```

#### 验证条件

找到引擎路径后，验证以下关键文件存在：

- `{EngineDir}/Build/BatchFiles/Build.bat`（编译入口）
- `{EngineDir}/Binaries/Win64/UnrealEditor.exe`（编辑器可执行文件）
- `{EngineDir}/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe`（构建工具）

#### 实现

```python
import winreg  # Windows 内置模块，无需额外依赖

def find_engine_path(uproject_path: str) -> str | None:
    """通过 .uproject 定位引擎安装路径。"""
    
    # 策略 1: 环境变量
    env_path = os.environ.get("UNREAL_ENGINE_PATH")
    if env_path and _validate_engine_dir(env_path):
        return env_path
    
    # 策略 2: 解析 EngineAssociation → 注册表
    with open(uproject_path) as f:
        uproject = json.load(f)
    engine_version = uproject.get("EngineAssociation", "")
    
    if engine_version and platform.system() == "Windows":
        try:
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                rf"SOFTWARE\EpicGames\Unreal Engine\{engine_version}"
            )
            install_dir, _ = winreg.QueryValueEx(key, "InstalledDirectory")
            if _validate_engine_dir(install_dir):
                return install_dir
        except (OSError, FileNotFoundError):
            pass
    
    # 策略 3: 常见路径扫描
    for drive in "CDEFG":
        candidate = f"{drive}:/Program Files/Epic Games/UE_{engine_version}"
        if _validate_engine_dir(candidate):
            return candidate
    
    return None

def _validate_engine_dir(path: str) -> bool:
    """验证引擎目录包含必要的可执行文件。"""
    return (
        os.path.isfile(os.path.join(path, "Engine/Build/BatchFiles/Build.bat"))
        or os.path.isfile(os.path.join(path, "Engine/Binaries/Win64/UnrealEditor.exe"))
    )
```

### 2. 编译项目（build_project）

#### 编译命令

Windows 平台上，UE 项目编译的标准方式：

```batch
"{EngineDir}/Engine/Build/BatchFiles/Build.bat" ^
    {ProjectName}Editor ^
    Win64 ^
    Development ^
    -Project="{uproject_path}" ^
    -WaitMutex -FromMsBuild
```

以 Aura 项目为例：

```batch
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ^
    AuraEditor ^
    Win64 ^
    Development ^
    -Project="I:/Aura/Aura.uproject" ^
    -WaitMutex -FromMsBuild
```

其中：
- `{ProjectName}Editor` — 项目名 + "Editor" 后缀，表示编译 Editor Target
- `Win64` — 目标平台
- `Development` — 编译配置（Development / DebugGame / Shipping）
- `-WaitMutex` — 等待编译互斥锁（防止并发编译冲突）
- `-FromMsBuild` — 标识从外部工具触发编译

#### 异步进度管理

编译是长时间操作（通常 30s ~ 10min），需要非阻塞设计：

```python
# 全局编译状态（MCP Server 进程级单例）
_build_state = {
    "status": "idle",           # idle / building / success / failed
    "progress_pct": 0,          # 编译进度百分比（如果可用）
    "current_target": "",       # 当前编译目标
    "start_time": None,         # 编译开始时间
    "elapsed_seconds": 0,       # 已用时间
    "output_lines": [],         # 最近 N 行编译输出（滚动窗口）
    "error_lines": [],          # 错误输出
    "return_code": None,        # 进程退出码
    "uproject_path": "",        # 编译的项目路径
}
```

#### 进度解析

UBT 输出中包含编译进度信息，格式如下：

```
[1/42] Compile XXX.cpp
[2/42] Compile YYY.cpp
...
[42/42] Link AuraEditor.dll
```

通过正则提取 `[current/total]` 计算进度百分比：

```python
import re

_PROGRESS_PATTERN = re.compile(r'\[(\d+)/(\d+)\]')

def _parse_progress(line: str) -> float | None:
    """从 UBT 输出行中提取编译进度。"""
    match = _PROGRESS_PATTERN.search(line)
    if match:
        current, total = int(match.group(1)), int(match.group(2))
        if total > 0:
            return (current / total) * 100
    return None
```

#### 执行流程

```python
@mcp.tool()
async def build_project(
    uproject_path: str,
    configuration: str = "Development",
    platform_name: str = "Win64",
    clean: bool = False,
) -> dict:
    """编译 UE 项目。
    
    启动后立即返回，通过 get_build_status 轮询进度。
    """
    global _build_state
    
    # 1. 防止重复编译
    if _build_state["status"] == "building":
        return {"error": "编译正在进行中，请等待完成或使用 get_build_status 查看进度"}
    
    # 2. 定位引擎路径
    engine_path = find_engine_path(uproject_path)
    if not engine_path:
        return {"error": "找不到 UE 引擎路径，请设置 UNREAL_ENGINE_PATH 环境变量"}
    
    # 3. 构造编译命令
    project_name = Path(uproject_path).stem  # "Aura"
    build_bat = os.path.join(engine_path, "Engine/Build/BatchFiles/Build.bat")
    
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
    
    # 4. 启动异步子进程
    _build_state = {
        "status": "building",
        "start_time": time.time(),
        ...
    }
    asyncio.create_task(_run_build_process(cmd))
    
    return {
        "status": "building",
        "message": f"编译已启动: {project_name}Editor ({platform_name}/{configuration})",
        "engine_path": engine_path,
    }
```

子进程管理：

```python
async def _run_build_process(cmd: list[str]) -> None:
    """在后台运行编译进程并持续更新状态。"""
    global _build_state
    
    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        
        # 实时读取 stdout
        async for line in proc.stdout:
            text = line.decode("utf-8", errors="replace").strip()
            if text:
                _build_state["output_lines"].append(text)
                # 保持滚动窗口
                if len(_build_state["output_lines"]) > 100:
                    _build_state["output_lines"].pop(0)
                # 更新进度
                pct = _parse_progress(text)
                if pct is not None:
                    _build_state["progress_pct"] = pct
        
        # 等待进程结束
        await proc.wait()
        _build_state["return_code"] = proc.returncode
        
        # 读取 stderr
        stderr_data = await proc.stderr.read()
        if stderr_data:
            _build_state["error_lines"] = stderr_data.decode(
                "utf-8", errors="replace"
            ).strip().split("\n")[-50:]  # 保留最后 50 行
        
        _build_state["status"] = "success" if proc.returncode == 0 else "failed"
        
    except Exception as e:
        _build_state["status"] = "failed"
        _build_state["error_lines"] = [str(e)]
    
    _build_state["elapsed_seconds"] = time.time() - _build_state["start_time"]
```

### 3. 启动编辑器（launch_editor）

#### 启动命令

```batch
"{EngineDir}/Engine/Binaries/Win64/UnrealEditor.exe" "{uproject_path}"
```

#### 等待 TCP 就绪

启动编辑器后，需要等待 UnrealAgent 插件的 TCP 服务初始化完成：

```python
@mcp.tool()
async def launch_editor(
    uproject_path: str,
    wait_for_connection: bool = True,
    timeout_seconds: float = 300.0,
) -> dict:
    """启动 UE 编辑器并加载项目。"""
    
    # 1. 检查编辑器是否已在运行（尝试 TCP 连接）
    try:
        result = await connection.send_request("get_project_info", {})
        return {
            "status": "already_running",
            "message": "编辑器已在运行",
            "project_info": result,
        }
    except (ConnectionError, OSError):
        pass  # 编辑器未运行，继续
    
    # 2. 定位引擎路径
    engine_path = find_engine_path(uproject_path)
    if not engine_path:
        return {"error": "找不到 UE 引擎路径"}
    
    # 3. 启动编辑器进程（detached，不阻塞 MCP Server）
    editor_exe = os.path.join(
        engine_path, "Engine/Binaries/Win64/UnrealEditor.exe"
    )
    subprocess.Popen(
        [editor_exe, uproject_path],
        creationflags=subprocess.DETACHED_PROCESS,
    )
    
    if not wait_for_connection:
        return {
            "status": "launched",
            "message": "编辑器已启动，但未等待 TCP 连接就绪",
        }
    
    # 4. 等待 TCP 连接就绪
    start = time.time()
    poll_interval = 3.0  # 每 3 秒尝试一次
    
    while time.time() - start < timeout_seconds:
        await asyncio.sleep(poll_interval)
        try:
            result = await connection.send_request("get_project_info", {})
            elapsed = round(time.time() - start, 1)
            return {
                "status": "ready",
                "message": f"编辑器已就绪（等待 {elapsed}s）",
                "project_info": result,
            }
        except (ConnectionError, OSError):
            continue
    
    return {
        "status": "timeout",
        "message": f"编辑器启动超时（{timeout_seconds}s），TCP 连接未就绪",
    }
```

### 4. 一条龙（build_and_launch）

```python
@mcp.tool()
async def build_and_launch(
    uproject_path: str,
    configuration: str = "Development",
    skip_build: bool = False,
) -> dict:
    """编译项目并启动编辑器。
    
    完整流程：编译 → 等待完成 → 启动编辑器 → 等待 TCP 就绪。
    """
    
    if not skip_build:
        # 1. 启动编译（同步等待完成）
        build_result = await _build_and_wait(uproject_path, configuration)
        if build_result["status"] == "failed":
            return {
                "status": "build_failed",
                "message": "编译失败，无法启动编辑器",
                "build_errors": build_result.get("error_lines", []),
            }
    
    # 2. 启动编辑器并等待就绪
    launch_result = await launch_editor(
        uproject_path=uproject_path,
        wait_for_connection=True,
    )
    
    return {
        "status": launch_result["status"],
        "message": "编译完成，编辑器已就绪" if launch_result["status"] == "ready"
                   else launch_result["message"],
        "build": build_result if not skip_build else {"skipped": True},
        "editor": launch_result,
    }
```

### 5. 查询编译状态（get_build_status）

```python
@mcp.tool()
async def get_build_status() -> dict:
    """查询当前编译状态和进度。"""
    
    state = dict(_build_state)
    
    if state["status"] == "building" and state["start_time"]:
        state["elapsed_seconds"] = round(time.time() - state["start_time"], 1)
    
    # 返回最近 20 行输出（避免 token 过长）
    state["recent_output"] = state.get("output_lines", [])[-20:]
    
    return state
```

---

## 整体流程图

```
┌─────────────────────────────────────────────────────────────┐
│                    AI Client (如 Claude)                     │
│  "帮我编译并打开 Aura 项目"                                   │
└────────────────────┬────────────────────────────────────────┘
                     │ MCP 调用
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              build_and_launch(uproject_path)                 │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Step 1: find_engine_path()                          │    │
│  │   .uproject → EngineAssociation: "5.7"              │    │
│  │   → 注册表 / 路径扫描                                │    │
│  │   → "C:/Program Files/Epic Games/UE_5.7"            │    │
│  └───────────────────┬─────────────────────────────────┘    │
│                      ▼                                      │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Step 2: build_project()                             │    │
│  │   subprocess: Build.bat AuraEditor Win64 Development │    │
│  │   实时输出: [1/42] Compile ... [42/42] Link ...     │    │
│  │   等待编译完成 (return_code == 0)                     │    │
│  └───────────────────┬─────────────────────────────────┘    │
│                      ▼                                      │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Step 3: launch_editor()                             │    │
│  │   subprocess: UnrealEditor.exe "I:/Aura/Aura.uproject"│  │
│  │   Popen(detached) — 不阻塞 MCP Server                │    │
│  └───────────────────┬─────────────────────────────────┘    │
│                      ▼                                      │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Step 4: wait_for_connection()                       │    │
│  │   loop: 每 3s 尝试 TCP connect :55557               │    │
│  │   最多等待 300s                                      │    │
│  │   → 连接成功 → get_project_info 验证                 │    │
│  └───────────────────┬─────────────────────────────────┘    │
│                      ▼                                      │
│  return {"status": "ready", "project_info": {...}}          │
└─────────────────────────────────────────────────────────────┘
```

---

## 文件修改清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `MCPServer/src/unreal_agent_mcp/tools/build.py` | **新建** | 4 个工具函数 + 引擎路径定位 + 异步进程管理 |
| `MCPServer/src/unreal_agent_mcp/__main__.py` | **修改** | 添加 `build` 模块导入 |

### 不需要修改的内容

- **不需要修改 C++ 插件代码** — 编译/启动操作在 OS 层面完成
- **不需要修改 connection.py** — 复用现有 TCP 连接做"就绪检测"
- **不需要修改 pyproject.toml** — `winreg`、`subprocess`、`asyncio` 均为 Python 内置模块

---

## 边界情况与容错

| 场景 | 处理策略 |
|------|---------|
| 引擎路径找不到 | 返回错误 + 提示设置 `UNREAL_ENGINE_PATH` 环境变量 |
| 编译失败 | 返回 `status: failed` + 最后 50 行编译错误日志 |
| 编辑器已在运行 | `launch_editor` 检测 TCP 端口，返回 `already_running` |
| 编译过程中重复调用 `build_project` | 返回当前编译状态，拒绝重复启动 |
| 编辑器启动超时（>300s） | 返回 `timeout`，提示手动检查 |
| .uproject 路径无效 | 预检查文件是否存在 |
| 非 Windows 平台 | 当前仅支持 Win64（Mac 支持为 v2 scope） |
| 编译进程意外终止 | 捕获异常，更新状态为 `failed` |
| MCP Server 在编译过程中被终止 | 子进程不会随 MCP 退出而自动终止（需文档说明） |

---

## 使用示例

### 场景 1：完整流程

用户："帮我编译并打开 Aura 项目"

```
AI → build_and_launch(uproject_path="I:/Aura/Aura.uproject")
← {"status": "building", "message": "编译已启动..."}

AI → get_build_status()
← {"status": "building", "progress_pct": 65, "recent_output": [...]}

AI → get_build_status()
← {"status": "success", "elapsed_seconds": 42.3}
  (内部自动启动编辑器并等待)

← {"status": "ready", "project_info": {"project_name": "Aura", ...}}
```

### 场景 2：只启动编辑器（已编译过）

用户："打开编辑器"

```
AI → launch_editor(uproject_path="I:/Aura/Aura.uproject")
← {"status": "ready", "message": "编辑器已就绪（等待 23.4s）"}
```

### 场景 3：只编译不启动

用户："编译一下项目"

```
AI → build_project(uproject_path="I:/Aura/Aura.uproject")
← {"status": "building", "message": "编译已启动..."}

AI → get_build_status()  (轮询)
← {"status": "success", "elapsed_seconds": 38.1}
```

---

## 后续演进

### v2 可扩展方向

1. **Mac 平台支持** — 替换 Build.bat 为 Build.sh，替换 winreg 为 plist 解析
2. **编译配置参数化** — 支持 DebugGame、Shipping 等配置
3. **热重载（Hot Reload）** — 编辑器运行中重新编译插件
4. **Live Coding 集成** — 通过 UE Live Coding 实现运行时增量编译
5. **编译缓存清理** — 清理 Intermediate 目录，强制全量重编
6. **多项目支持** — 允许同时管理多个 .uproject 的编译和启动

---

## 日期

- 设计日期：2026-03-10
- 状态：待实现
