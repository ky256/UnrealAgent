# UnrealAgent

AI Agent control interface for Unreal Editor via [MCP](https://modelcontextprotocol.io/) (Model Context Protocol).

Let AI assistants (Claude, Cursor, Windsurf, CodeBuddy, etc.) directly query and control your Unreal Editor — create actors, manage assets, execute Python, and more.

```
AI Client (Claude / Cursor / Windsurf / CodeBuddy)
    │  MCP (stdio)
    ▼
Python MCP Server
    │  TCP :55557
    ▼
UE Plugin (C++ Editor Module)
    │  JSON-RPC 2.0
    ▼
Unreal Editor API
```

---

## Quick Start

> **Just paste this to your AI assistant:**
>
> *"帮我部署这个项目 https://github.com/ky256/UnrealAgent"*
>
> or in English:
>
> *"Help me deploy this project: https://github.com/ky256/UnrealAgent"*
>
> The AI will read this page and complete the setup automatically.

---

## Setup Guide (for AI-assisted deployment)

This section is designed to be read and executed by an AI assistant. Each step includes verification commands.

### Prerequisites

- Unreal Engine 5.5+ with the project open in the Editor
- Python 3.10+ (`python --version` to check)
- One of: `uv`, `pip`, or `pipx` for Python package management

### Step 1: Install the UE Plugin

**Option A — Git Submodule (recommended for version control):**

```bash
cd <your-ue-project-root>
git submodule add https://github.com/ky256/UnrealAgent.git Plugins/UnrealAgent
```

**Option B — Direct clone:**

```bash
cd <your-ue-project-root>/Plugins
git clone https://github.com/ky256/UnrealAgent.git
```

**Option C — If the project already includes UnrealAgent as a submodule:**

```bash
git submodule update --init --recursive
```

After adding the plugin, **restart the Unreal Editor**. The plugin auto-enables and starts its TCP server on port 55557.

**Verify:** In the UE Editor, go to **Edit → Project Settings → Plugins → UnrealAgent** — you should see the settings panel. The Output Log should show `LogUnrealAgent: TCP server listening on 127.0.0.1:55557`.

### Step 2: Install the MCP Server

```bash
cd <your-ue-project-root>/Plugins/UnrealAgent/MCPServer
pip install -e .
```

Or with `uv` (faster):

```bash
cd <your-ue-project-root>/Plugins/UnrealAgent/MCPServer
uv pip install -e .
```

**Verify:** Run `python -m unreal_agent_mcp --help` or `unreal-agent-mcp --help`. It should not error.

### Step 3: Configure your AI client

Add the MCP server configuration to your AI client. Pick the one you use:

#### Claude Desktop

Edit `%APPDATA%\Claude\claude_desktop_config.json` (Windows) or `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS):

```json
{
  "mcpServers": {
    "unreal-agent": {
      "command": "python",
      "args": ["-m", "unreal_agent_mcp"],
      "env": {
        "UNREAL_AGENT_HOST": "127.0.0.1",
        "UNREAL_AGENT_PORT": "55557"
      }
    }
  }
}
```

> If the file already exists and has other MCP servers, merge the `"unreal-agent"` entry into the existing `"mcpServers"` object. Do not overwrite other entries.

#### Cursor

Edit `.cursor/mcp.json` in your project root (or global `~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "unreal-agent": {
      "command": "python",
      "args": ["-m", "unreal_agent_mcp"],
      "env": {
        "UNREAL_AGENT_HOST": "127.0.0.1",
        "UNREAL_AGENT_PORT": "55557"
      }
    }
  }
}
```

#### Windsurf

Edit `~/.codeium/windsurf/mcp_config.json`:

```json
{
  "mcpServers": {
    "unreal-agent": {
      "command": "python",
      "args": ["-m", "unreal_agent_mcp"],
      "env": {
        "UNREAL_AGENT_HOST": "127.0.0.1",
        "UNREAL_AGENT_PORT": "55557"
      }
    }
  }
}
```

#### CodeBuddy

```bash
codebuddy mcp add unreal-agent -- python -m unreal_agent_mcp
```

#### Other MCP-compatible clients

Any client that supports the MCP stdio transport can use:
- **Command:** `python -m unreal_agent_mcp`
- **Transport:** stdio
- **Environment:** `UNREAL_AGENT_HOST=127.0.0.1`, `UNREAL_AGENT_PORT=55557`

### Step 4: Restart and verify

1. **Restart your AI client** (close and reopen).
2. Make sure Unreal Editor is running with your project open.
3. Ask the AI: **"What's my current UE project name?"**
4. The AI should call `get_project_info` and return your project details.

If it works, you're done!

### Troubleshooting

| Symptom | Fix |
|---------|-----|
| AI says "cannot connect to UnrealAgent" | Make sure UE Editor is running and the plugin is loaded. Check Output Log for `TCP server listening on 127.0.0.1:55557`. |
| AI doesn't see UnrealAgent tools | Restart the AI client after editing the config file. Check config JSON syntax. |
| `python -m unreal_agent_mcp` not found | Make sure you ran `pip install -e .` in the MCPServer directory. Check that the correct Python is on your PATH. |
| Python execution returns "PythonScriptPlugin not loaded" | Enable the Python Editor Script Plugin: Edit → Plugins → search "Python" → enable → restart editor. |

---

## Available Tools (19)

| Group | Tool | Description |
|-------|------|-------------|
| **Project** | `get_project_info` | Project name, engine version, modules, plugins |
| **Project** | `get_editor_state` | Active level, PIE status, selected actors |
| **Asset** | `list_assets` | List assets by path, class filter, recursive |
| **Asset** | `search_assets` | Search assets by name |
| **Asset** | `get_asset_info` | Asset metadata and tags |
| **Asset** | `get_asset_references` | Referencers and dependencies graph |
| **World** | `get_world_outliner` | All actors in level with properties |
| **World** | `get_current_level` | Level name, path, streaming sub-levels |
| **World** | `get_actor_details` | Full actor transform, components, tags |
| **Actor** | `create_actor` | Spawn actor by class with transform |
| **Actor** | `delete_actor` | Remove actor from level |
| **Actor** | `select_actors` | Select/deselect actors in editor |
| **Viewport** | `get_viewport_camera` | Camera position and rotation |
| **Viewport** | `move_viewport_camera` | Set camera position/rotation |
| **Viewport** | `focus_on_actor` | Focus viewport on specific actor |
| **Editor** | `undo` | Undo last editor operation(s) |
| **Editor** | `redo` | Redo last undone operation(s) |
| **Python** | `execute_python` | Execute arbitrary Python in UE Editor context |
| **Python** | `reset_python_context` | Reset shared Python execution context |

### The `execute_python` Tool

This is the universal execution layer — AI can run any Python code with access to the full `unreal` module API. Key features:

- **Stateful** — variables and imports persist across calls
- **Timeout protected** — default 30s, max 120s (prevents infinite loops from freezing the editor)
- **Undo support** — each execution is wrapped in a named transaction (Ctrl+Z to revert)
- **Named transactions** — AI can set a descriptive name that appears in Edit → Undo History

If a dedicated tool doesn't exist for something, AI can always fall back to `execute_python` to accomplish it.

---

## Plugin Settings

Edit → Project Settings → Plugins → UnrealAgent

| Setting | Default | Description |
|---------|---------|-------------|
| ServerPort | 55557 | TCP listen port |
| bAutoStart | true | Auto-start server on editor launch |
| BindAddress | 127.0.0.1 | Bind address (127.0.0.1 = local only) |
| MaxConnections | 16 | Max concurrent TCP connections |
| bVerboseLogging | false | Enable detailed logging |

---

## Telemetry (opt-in)

UnrealAgent can optionally collect anonymized usage patterns to improve the tool. **This is disabled by default.**

To enable, set the environment variable:

```
UNREAL_AGENT_TELEMETRY=1
```

What is collected: structural code fingerprints and execution stats (never raw code). See `telemetry.py` for details.

---

## Project Structure

```
UnrealAgent/
├── UnrealAgent.uplugin
├── Config/DefaultUnrealAgent.ini
├── Source/UnrealAgent/
│   ├── Public/
│   │   ├── Commands/   ← Tool implementations (one class per group)
│   │   ├── Server/     ← TCP server + client connection
│   │   ├── Protocol/   ← JSON-RPC 2.0 handler
│   │   └── Settings/   ← Plugin configuration
│   └── Private/        ← Corresponding .cpp files
├── MCPServer/
│   ├── pyproject.toml
│   └── src/unreal_agent_mcp/
│       ├── server.py        ← FastMCP instance
│       ├── connection.py    ← TCP client to UE plugin
│       ├── ast_fingerprint.py  ← Code structural analysis
│       ├── telemetry.py     ← Optional anonymous telemetry
│       └── tools/           ← MCP tool definitions
└── Docs/
```

## License

MIT License — see LICENSE file.
