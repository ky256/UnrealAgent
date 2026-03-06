"""知识库存储层 — Phase 1: JSON 文件实现。

提供统一的 KnowledgeStore 抽象接口，底层存储可从 JSON 平滑迁移到 SQLite/向量引擎。
"""

import json
import logging
import os
import re
import uuid
from abc import ABC, abstractmethod
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

# 知识库根目录（MCPServer/knowledge/）
KNOWLEDGE_DIR = Path(__file__).resolve().parent.parent.parent / "knowledge"


@dataclass
class KnowledgeEntry:
    """统一知识条目 Schema — 跨所有阶段不变。"""

    id: str
    category: str  # api_reference / lessons_learned / best_practices / ue_api_gotchas / tool_schemas
    title: str
    content: str  # 详细内容（Markdown 或纯文本）
    tags: list[str] = field(default_factory=list)
    ue_version: str | None = None
    confidence: float = 1.0  # 0~1，AI 自动写入的默认 0.8
    source: str = "manual"  # manual / ai_generated / imported
    created_at: str = ""
    updated_at: str = ""
    access_count: int = 0

    def __post_init__(self):
        now = datetime.now(timezone.utc).isoformat()
        if not self.created_at:
            self.created_at = now
        if not self.updated_at:
            self.updated_at = now


class KnowledgeStore(ABC):
    """抽象接口 — 存储层可替换，上层代码不需要改。"""

    @abstractmethod
    def search(
        self,
        query: str,
        category: str | None = None,
        tags: list[str] | None = None,
        top_k: int = 10,
    ) -> list[KnowledgeEntry]:
        """检索知识条目。"""
        ...

    @abstractmethod
    def get_by_id(self, entry_id: str) -> KnowledgeEntry | None:
        """按 ID 精确获取。"""
        ...

    @abstractmethod
    def save(self, entry: KnowledgeEntry) -> str:
        """保存知识条目，返回 ID。"""
        ...

    @abstractmethod
    def update(self, entry_id: str, updates: dict) -> bool:
        """部分更新知识条目。"""
        ...

    @abstractmethod
    def delete(self, entry_id: str) -> bool:
        """删除知识条目。"""
        ...

    @abstractmethod
    def list_categories(self) -> list[str]:
        """列出所有可用的知识分类。"""
        ...


class JsonKnowledgeStore(KnowledgeStore):
    """Phase 1 实现 — JSON 文件存储，< 500 条知识时性能充足。

    目录结构：
        knowledge/
        ├── api_reference/          # 静态 JSON，按类别分文件
        ├── ue_api_gotchas/         # UE 引擎注意事项
        ├── tool_schemas/           # 工具使用指南
        ├── lessons_learned/        # 经验知识
        ├── best_practices/         # 最佳实践
        └── _index.json             # 倒排索引（自动维护）
    """

    def __init__(self, knowledge_dir: Path | None = None):
        self.root = knowledge_dir or KNOWLEDGE_DIR
        self.root.mkdir(parents=True, exist_ok=True)
        self._index_path = self.root / "_index.json"
        # 缓存：category -> { entry_id -> KnowledgeEntry }
        self._cache: dict[str, dict[str, KnowledgeEntry]] = {}
        # 倒排索引：keyword -> set[entry_id]
        self._inverted: dict[str, set[str]] = {}
        self._loaded = False

    def _ensure_loaded(self):
        """延迟加载所有知识文件到内存。"""
        if self._loaded:
            return
        self._cache.clear()
        self._inverted.clear()

        for category_dir in self.root.iterdir():
            if not category_dir.is_dir() or category_dir.name.startswith("_"):
                continue
            category = category_dir.name
            self._cache[category] = {}

            for json_file in category_dir.glob("*.json"):
                try:
                    data = json.loads(json_file.read_text(encoding="utf-8"))
                    entries = self._parse_file(data, category, json_file.stem)
                    for entry in entries:
                        self._cache[category][entry.id] = entry
                        self._index_entry(entry)
                except Exception as e:
                    logger.warning(f"加载知识文件失败 {json_file}: {e}")

        self._loaded = True
        logger.info(
            f"知识库已加载: {sum(len(v) for v in self._cache.values())} 条目, "
            f"{len(self._inverted)} 个索引关键词"
        )

    def _parse_file(
        self, data: dict | list, category: str, file_stem: str
    ) -> list[KnowledgeEntry]:
        """解析 JSON 文件为 KnowledgeEntry 列表。

        支持两种格式：
        1. 单个条目 dict（有 id 字段）
        2. 包含 entries 数组的 dict
        3. 带嵌套结构的 API 参考文件（key 是条目 ID）
        """
        entries = []

        # 格式 1：直接的条目 dict
        if isinstance(data, dict) and "id" in data and "title" in data:
            entries.append(self._dict_to_entry(data, category))

        # 格式 2：entries 数组
        elif isinstance(data, dict) and "entries" in data:
            for item in data["entries"]:
                if isinstance(item, dict) and "id" in item:
                    item.setdefault("category", data.get("category", category))
                    entries.append(self._dict_to_entry(item, category))

        # 格式 3：API 参考——每个 key 是一个独立条目
        elif isinstance(data, dict):
            for key, value in data.items():
                if isinstance(value, dict):
                    entry_id = f"{file_stem}_{key}" if "id" not in value else value["id"]
                    entry = KnowledgeEntry(
                        id=entry_id,
                        category=category,
                        title=value.get("title", key),
                        content=json.dumps(value, ensure_ascii=False, indent=2),
                        tags=value.get("tags", [key.lower()]),
                        ue_version=value.get("ue_version"),
                        confidence=value.get("confidence", 1.0),
                        source=value.get("source", "manual"),
                    )
                    entries.append(entry)

        return entries

    def _dict_to_entry(self, d: dict, default_category: str) -> KnowledgeEntry:
        """将 dict 转换为 KnowledgeEntry。"""
        return KnowledgeEntry(
            id=d.get("id", str(uuid.uuid4())[:8]),
            category=d.get("category", default_category),
            title=d.get("title", ""),
            content=d.get("content", d.get("solution", d.get("description", ""))),
            tags=d.get("tags", []),
            ue_version=d.get("ue_version"),
            confidence=d.get("confidence", 1.0),
            source=d.get("source", "manual"),
            created_at=d.get("created_at", ""),
            updated_at=d.get("updated_at", ""),
            access_count=d.get("access_count", 0),
        )

    def _index_entry(self, entry: KnowledgeEntry):
        """将条目加入倒排索引。"""
        keywords = set()
        # 从标签提取
        for tag in entry.tags:
            keywords.add(tag.lower())
        # 从标题提取单词
        for word in re.split(r"[\s_\-./()（）]+", entry.title.lower()):
            if len(word) >= 2:
                keywords.add(word)
        # 从 ID 提取
        for part in entry.id.lower().split("_"):
            if len(part) >= 2:
                keywords.add(part)

        for kw in keywords:
            if kw not in self._inverted:
                self._inverted[kw] = set()
            self._inverted[kw].add(entry.id)

    def search(
        self,
        query: str,
        category: str | None = None,
        tags: list[str] | None = None,
        top_k: int = 10,
    ) -> list[KnowledgeEntry]:
        """混合搜索：倒排索引 + 全文匹配 + tag 过滤。"""
        self._ensure_loaded()

        # 1. 收集候选条目
        candidates: dict[str, float] = {}  # entry_id -> 得分

        query_lower = query.lower()
        query_words = [w for w in re.split(r"[\s_\-./()]+", query_lower) if len(w) >= 2]

        # 1a. 倒排索引匹配
        for word in query_words:
            for kw, entry_ids in self._inverted.items():
                if word in kw or kw in word:
                    for eid in entry_ids:
                        candidates[eid] = candidates.get(eid, 0) + (
                            3.0 if word == kw else 1.5
                        )

        # 1b. 全文匹配（标题和内容）
        for cat, entries in self._cache.items():
            if category and cat != category:
                continue
            for eid, entry in entries.items():
                score = 0.0
                # 标题精确包含
                if query_lower in entry.title.lower():
                    score += 5.0
                # 内容包含
                if query_lower in entry.content.lower():
                    score += 2.0
                # 单词匹配
                for word in query_words:
                    if word in entry.title.lower():
                        score += 1.0
                    if word in entry.content.lower():
                        score += 0.5
                if score > 0:
                    candidates[eid] = candidates.get(eid, 0) + score

        # 2. Tag 过滤
        if tags:
            tag_set = {t.lower() for t in tags}
            candidates = {
                eid: score
                for eid, score in candidates.items()
                if self._get_entry(eid)
                and tag_set.intersection(
                    t.lower() for t in self._get_entry(eid).tags
                )
            }

        # 3. 分类过滤
        if category:
            candidates = {
                eid: score
                for eid, score in candidates.items()
                if self._get_entry(eid) and self._get_entry(eid).category == category
            }

        # 4. 排序并返回
        sorted_ids = sorted(candidates, key=lambda eid: candidates[eid], reverse=True)
        results = []
        for eid in sorted_ids[:top_k]:
            entry = self._get_entry(eid)
            if entry:
                entry.access_count += 1
                results.append(entry)

        return results

    def get_by_id(self, entry_id: str) -> KnowledgeEntry | None:
        """按 ID 精确获取。"""
        self._ensure_loaded()
        return self._get_entry(entry_id)

    def _get_entry(self, entry_id: str) -> KnowledgeEntry | None:
        """从缓存中查找条目。"""
        for entries in self._cache.values():
            if entry_id in entries:
                return entries[entry_id]
        return None

    def save(self, entry: KnowledgeEntry) -> str:
        """保存知识条目到对应分类目录。"""
        self._ensure_loaded()

        if not entry.id:
            entry.id = str(uuid.uuid4())[:8]

        # 确保分类目录存在
        cat_dir = self.root / entry.category
        cat_dir.mkdir(parents=True, exist_ok=True)

        # 添加到缓存
        if entry.category not in self._cache:
            self._cache[entry.category] = {}
        self._cache[entry.category][entry.id] = entry
        self._index_entry(entry)

        # 保存到文件（AI 生成的经验保存到 _ai_generated.json）
        if entry.source == "ai_generated":
            self._save_to_ai_file(entry)
        else:
            self._save_to_file(entry)

        logger.info(f"知识条目已保存: [{entry.id}] {entry.title}")
        return entry.id

    def _save_to_ai_file(self, entry: KnowledgeEntry):
        """将 AI 生成的条目追加到 _ai_generated.json。"""
        ai_file = self.root / entry.category / "_ai_generated.json"
        existing = {"entries": []}
        if ai_file.exists():
            try:
                existing = json.loads(ai_file.read_text(encoding="utf-8"))
            except Exception:
                pass

        # 去重
        existing["entries"] = [
            e for e in existing["entries"] if e.get("id") != entry.id
        ]
        existing["entries"].append(asdict(entry))
        existing["category"] = entry.category

        ai_file.write_text(
            json.dumps(existing, ensure_ascii=False, indent=2), encoding="utf-8"
        )

    def _save_to_file(self, entry: KnowledgeEntry):
        """将手动条目保存到独立文件。"""
        file_path = self.root / entry.category / f"{entry.id}.json"
        file_path.write_text(
            json.dumps(asdict(entry), ensure_ascii=False, indent=2), encoding="utf-8"
        )

    def update(self, entry_id: str, updates: dict) -> bool:
        """部分更新知识条目。"""
        self._ensure_loaded()
        entry = self._get_entry(entry_id)
        if not entry:
            return False

        for key, value in updates.items():
            if hasattr(entry, key) and key not in ("id", "category"):
                setattr(entry, key, value)
        entry.updated_at = datetime.now(timezone.utc).isoformat()

        # 重新保存
        if entry.source == "ai_generated":
            self._save_to_ai_file(entry)
        else:
            self._save_to_file(entry)
        return True

    def delete(self, entry_id: str) -> bool:
        """删除知识条目。"""
        self._ensure_loaded()
        for cat, entries in self._cache.items():
            if entry_id in entries:
                entry = entries.pop(entry_id)
                # 清理倒排索引
                for kw_set in self._inverted.values():
                    kw_set.discard(entry_id)
                # 删除文件
                file_path = self.root / cat / f"{entry_id}.json"
                if file_path.exists():
                    file_path.unlink()
                # 如果在 AI 文件中，也要清理
                ai_file = self.root / cat / "_ai_generated.json"
                if ai_file.exists():
                    try:
                        data = json.loads(ai_file.read_text(encoding="utf-8"))
                        data["entries"] = [
                            e for e in data["entries"] if e.get("id") != entry_id
                        ]
                        ai_file.write_text(
                            json.dumps(data, ensure_ascii=False, indent=2),
                            encoding="utf-8",
                        )
                    except Exception:
                        pass
                logger.info(f"知识条目已删除: [{entry_id}]")
                return True
        return False

    def list_categories(self) -> list[str]:
        """列出所有可用的知识分类。"""
        self._ensure_loaded()
        return sorted(self._cache.keys())

    def get_stats(self) -> dict:
        """获取知识库统计信息。"""
        self._ensure_loaded()
        stats = {
            "total_entries": sum(len(v) for v in self._cache.values()),
            "categories": {},
            "index_keywords": len(self._inverted),
        }
        for cat, entries in self._cache.items():
            stats["categories"][cat] = len(entries)
        return stats

    def reload(self):
        """强制重新加载所有知识文件。"""
        self._loaded = False
        self._ensure_loaded()


# 全局单例
_store: JsonKnowledgeStore | None = None


def get_knowledge_store() -> JsonKnowledgeStore:
    """获取全局知识库实例。"""
    global _store
    if _store is None:
        _store = JsonKnowledgeStore()
    return _store
