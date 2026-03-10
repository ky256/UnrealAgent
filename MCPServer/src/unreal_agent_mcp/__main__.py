"""Entry point for the UnrealAgent MCP Server."""

import asyncio
import os
import sys


def main():
    """Run the MCP server."""
    from .server import mcp

    # Import tools to register them
    from .tools import project, assets, world, actors, viewport, python, editor, knowledge, materials  # noqa: F401
    from .tools import context, properties  # noqa: F401
    from .tools import blueprints, asset_manage  # noqa: F401
    from .tools import screenshots, events  # noqa: F401
    from .tools import build  # noqa: F401

    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
