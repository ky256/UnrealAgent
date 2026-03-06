"""知识库查询与写入 MCP 工具。

提供 query_knowledge 和 save_knowledge 两个工具，
让 AI 在操作前先查询知识库，操作后可写入新经验。
"""

import json
import logging
from dataclasses import asdict

from ..server import mcp
from ..knowledge_store import KnowledgeEntry, get_knowledge_store
from .python import record_tool_call

logger = logging.getLogger(__name__)


@mcp.tool()
async def query_knowledge(
    query: str,
    category: str = "",
    tags: str = "",
    top_k: int = 10,
) -> dict:
    """Search the UnrealAgent knowledge base for relevant information.

    Use BEFORE performing operations to:
    - Look up material expression types, properties, and pin definitions
    - Check for known UE API gotchas and breaking changes
    - Find best practices and recommended workflows
    - Review lessons learned from past operations

    Args:
        query: Search keywords or question (e.g., "MaterialExpressionCustom",
               "Custom HLSL node setup", "UE 5.7 API changes").
        category: Optional category filter. One of:
                  "api_reference" - UE API types, enums, expression properties
                  "ue_api_gotchas" - UE engine breaking changes and warnings
                  "tool_schemas" - Tool usage guides and parameter formats
                  "lessons_learned" - Past issues and their solutions
                  "best_practices" - Recommended workflows and checklists
                  Leave empty to search all categories.
        tags: Optional comma-separated tag filter (e.g., "material,compilation").
        top_k: Maximum number of results to return (default 10).
    """
    record_tool_call("query_knowledge")

    store = get_knowledge_store()
    tag_list = [t.strip() for t in tags.split(",") if t.strip()] if tags else None
    cat = category if category else None

    results = store.search(query, category=cat, tags=tag_list, top_k=top_k)

    if not results:
        return {
            "found": 0,
            "message": f"No knowledge found for query '{query}'"
            + (f" in category '{category}'" if category else ""),
            "suggestion": "Try broader keywords or different category. "
            "Available categories: api_reference, ue_api_gotchas, "
            "tool_schemas, lessons_learned, best_practices",
        }

    entries = []
    for entry in results:
        # 尝试将 content 解析为 JSON（如果是结构化数据的话直接返回对象）
        try:
            content = json.loads(entry.content)
        except (json.JSONDecodeError, TypeError):
            content = entry.content

        entries.append(
            {
                "id": entry.id,
                "category": entry.category,
                "title": entry.title,
                "content": content,
                "tags": entry.tags,
                "confidence": entry.confidence,
            }
        )

    return {
        "found": len(entries),
        "entries": entries,
    }


@mcp.tool()
async def save_knowledge(
    title: str,
    content: str,
    category: str = "lessons_learned",
    tags: str = "",
    ue_version: str = "",
    confidence: float = 0.8,
) -> dict:
    """Save new knowledge to the UnrealAgent knowledge base.

    Use AFTER successfully solving a new problem to record the experience
    for future reference. AI-generated entries are saved with confidence=0.8
    and can be reviewed/promoted by the user.

    Args:
        title: Brief, descriptive title (e.g., "Custom HLSL node InputName must match code variables").
        content: Detailed explanation. Include:
                 - The problem encountered
                 - The root cause
                 - The solution/workaround
                 - Any relevant code snippets
        category: Knowledge category. One of:
                  "lessons_learned" (default) - Issues and their solutions
                  "best_practices" - Recommended workflows
                  Only these two categories accept AI-generated content.
        tags: Comma-separated tags (e.g., "material,custom-node,hlsl").
        ue_version: UE version this applies to (e.g., "5.7").
        confidence: Confidence level 0-1 (default 0.8 for AI-generated).
    """
    record_tool_call("save_knowledge")

    # 只允许写入经验类别
    allowed_categories = {"lessons_learned", "best_practices"}
    if category not in allowed_categories:
        return {
            "success": False,
            "error": f"Category '{category}' is read-only. "
            f"Writable categories: {', '.join(allowed_categories)}",
        }

    store = get_knowledge_store()
    tag_list = [t.strip() for t in tags.split(",") if t.strip()] if tags else []

    entry = KnowledgeEntry(
        id="",  # 自动生成
        category=category,
        title=title,
        content=content,
        tags=tag_list,
        ue_version=ue_version if ue_version else None,
        confidence=min(max(confidence, 0.0), 1.0),
        source="ai_generated",
    )

    entry_id = store.save(entry)
    return {
        "success": True,
        "entry_id": entry_id,
        "message": f"Knowledge saved: [{entry_id}] {title}",
        "category": category,
        "tags": tag_list,
    }


@mcp.tool()
async def get_knowledge_stats() -> dict:
    """Get knowledge base statistics.

    Returns the total number of entries, categories, and index size.
    Useful for understanding what knowledge is available.
    """
    record_tool_call("get_knowledge_stats")
    store = get_knowledge_store()
    stats = store.get_stats()
    stats["available_categories"] = store.list_categories()
    return stats
