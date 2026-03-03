# Development Log

## 2026-03-03 — v0.1.0 Initial Implementation

### Phase 1: Plugin Skeleton
- Created `UnrealAgent.uplugin` (Editor type, Category: KuoYu)
- Module entry point with auto-start TCP server and editor menu registration
- `UUASettings` developer settings (port, auto-start, bind address, max connections)

### Phase 2: Communication Layer
- `UATcpServer`: FTcpListener + FTSTicker (~100Hz game thread tick)
- `UAClientConnection`: Non-blocking socket with Content-Length message framing
- `UAJsonRpcHandler`: JSON-RPC 2.0 parser/dispatcher with standard error codes

### Phase 3: Command System
- `UACommandBase`: Abstract base class with method registration and JSON Schema generation
- `UACommandRegistry`: Method-name-to-handler dispatch map with `list_tools` built-in

### Phase 4: Tool Commands (15 tools)
- **Project**: get_project_info, get_editor_state
- **Asset**: list_assets, search_assets, get_asset_info, get_asset_references
- **World**: get_world_outliner, get_current_level, get_actor_details
- **Actor**: create_actor, delete_actor, select_actors
- **Viewport**: get_viewport_camera, move_viewport_camera, focus_on_actor

### Phase 5: Python MCP Server
- FastMCP server with stdio transport
- TCP client with Content-Length framing and auto-reconnect
- MCP Resources: `unreal://project/info`, `unreal://editor/state`
- All 15 tools registered as MCP tools

### Compilation Fixes (UE 5.7 API Changes)
| File | Issue | Fix |
|------|-------|-----|
| UATcpServer.h | FIPv4Endpoint undefined | Added `#include "Interfaces/IPv4/IPv4Endpoint.h"` |
| UAActorCommands.cpp | ANY_PACKAGE removed in UE 5.7 | Replaced with `FindFirstObject<UClass>()` |
| UAActorCommands.cpp | SpawnActor takes references not pointers | Changed `&Location, &Rotation` to `Location, Rotation` |
| UAProjectCommands.cpp | USelection incomplete type | Added `#include "Selection.h"` |
| UAAssetCommands.cpp | GetTagsAndValues() removed in UE 5.7 | Replaced with `EnumerateTags()` + lambda |

### Connection Stability Fix
- **Problem**: Dead TCP connections not cleaned up, causing "max connections reached" rejection
- **Root cause**: `GetConnectionState()` unreliable for detecting dead non-blocking sockets
- **Fix**: Added Recv+Peek probe for dead connection detection; cleanup stale connections before accepting new ones; raised default MaxConnections 4→16

### Additional Compilation Fixes (Dead Connection Detection)
| Issue | Fix |
|-------|-----|
| `ESocketWaitConditions::WaitForReadOrError` doesn't exist | Changed to `ESocketWaitConditions::WaitForRead` |
| `HasPendingData(bool)` parameter type mismatch | Used `GetConnectionState()` for error check |
| Unused variable `bIsReadable` | Removed |

### Full Test Results — 15/15 Passed
All tools tested via CodeBuddy MCP integration against live Aura project (UE 5.7.1).

### Editor Crash Fix — TypedElement Assert
- **Symptom**: `delete_actor` on a selected actor triggers assert: `Element type ID '0' has not been registered!`
- **Root cause**: UE 5.7's USelection holds TypedElement handles; destroying an actor without deselecting leaves stale handles
- **Fix**: Call `GEditor->SelectActor(Actor, false, true)` before `Actor->Destroy()` in `UAActorCommands.cpp`

### Repository Split
- Extracted UnrealAgent from the Aura monorepo into a standalone repository: https://github.com/ky256/UnrealAgent
- Aura now references UnrealAgent as a git submodule at `Plugins/UnrealAgent`
- Python virtual environment (`.venv`) and build artifacts (`Binaries/`, `Intermediate/`) remain git-ignored and local only

---

## Backlog / Future Work
- [ ] Blueprint read/write commands (UABlueprintCommands)
- [ ] Material/Texture inspection tools
- [ ] Screenshot/thumbnail capture
- [ ] PIE (Play In Editor) control (start/stop/pause)
- [ ] Console command execution
- [ ] Asset creation/import tools
- [ ] Undo/redo support
- [ ] Multi-level support
- [ ] Remote connection support (non-localhost)
- [ ] Authentication for remote connections
