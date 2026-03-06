
# UnrealAgent 知识库设计文档

> **版本**: v1.0  
> **日期**: 2026-03-06  
> **状态**: 设计阶段

---

## 一、设计目标

UnrealAgent 当前是一个"增强型 LLM"架构——LLM 通过 MCP Server 调用 UE 编辑器的工具（30 个命令）。但它缺少关键一环：**知识/记忆层**。

没有知识库时 AI 会反复踩坑（如 UE 5.7 的 `ANY_PACKAGE_COMPAT` 移除、`GetExpressions()` 返回类型变更等），有了知识库后 AI 可以在操作前先查阅，避免重复试错。

### 目标架构

```
┌──────────────┐
│   AI / LLM   │
└──┬───────┬───┘
   │       │
   ▼       ▼
┌──────┐ ┌──────────┐
│知识库│ │ UE 工具  │
│      │ │ 30个命令 │
└──────┘ └──────────┘
```

AI 在执行操作前先查询知识库获取上下文（Just-in-Time Retrieval），执行后将新经验写回知识库。

---

## 二、设计原则

### 2.1 Anthropic 的 Augmented LLM 模型

| 层 | 作用 | 对应 UnrealAgent |
|---|------|-----------------|
| **LLM** | 推理与决策 | Claude / GPT |
| **Tools** | 执行操作 | 30 个 UE 命令 |
| **Retrieval** | 按需获取知识 | ← 需要建设 |
| **Memory** | 跨会话记忆 | ← 需要建设 |

### 2.2 核心原则

1. **Just-in-Time Retrieval**：不要把所有知识塞进 System Prompt，让 Agent 在需要时主动查询
2. **存储层可替换，检索接口不变**：上层 Agent 代码不因底层存储升级而修改
3. **渐进式扩展**：从简单 JSON 文件开始，按需迁移到 SQLite 和向量引擎
4. **知识有生命周期**：自动归档冷知识、标记过期知识、去重相似知识

### 2.3 记忆分层策略（参考 Anthropic Memory API）

| 层级 | 类型 | 访问频率 | 内容示例 |
|------|------|---------|---------|
| **Hot（热）** | 即时上下文 | 每次调用 | 当前材质节点图、正在编辑的资产状态 |
| **Warm（温）** | 近期记忆 | 按需检索 | 最近执行过的操作序列、上次编译错误 |
| **Cold（冷）** | 归档知识 | 偶尔检索 | UE API 踩坑记录、材质表达式类型大全 |

---

## 三、知识库架构

### 3.1 三层知识结构

```
知识库/
├── Layer 1: 静态知识（API 参考 / 类型映射）
├── Layer 2: 经验知识（踩坑记录 / 最佳实践）
└── Layer 3: 动态知识（项目状态 / 执行历史）
```

### 3.2 目录结构

```
knowledge/
├── api_reference/                          # Layer 1: 静态知识
│   ├── material_expressions.json           # 材质表达式类型 + 输入输出 pin 定义
│   ├── material_properties.json            # EMaterialProperty 枚举映射
│   ├── blend_modes.json                    # EBlendMode 枚举映射
│   ├── shading_models.json                 # EMaterialShadingModel 枚举映射
│   ├── custom_output_types.json            # ECustomMaterialOutputType 枚举映射
│   └── common_expression_properties.json   # 常用表达式的可设置属性名和类型
│
├── ue_api_gotchas/                         # UE 引擎特殊注意事项
│   ├── ue57_breaking_changes.json          # UE 5.7 破坏性变更
│   └── deprecated_apis.json                # 已废弃的 API
│
├── tool_schemas/                           # 工具使用指南
│   └── tool_usage_guide.json               # 每个工具的详细使用指南和示例
│
├── lessons_learned/                        # Layer 2: 经验知识
│   ├── compilation.json                    # 编译相关经验
│   ├── hot_reload.json                     # 热加载局限性
│   ├── large_file_writing.json             # 大文件写入策略
│   ├── testing.json                        # 测试脚本注意事项
│   └── custom_hlsl_node.json               # Custom HLSL 节点操作要点
│
├── best_practices/                         # 最佳实践
│   ├── material_workflow.json              # 材质编辑完整工作流
│   ├── command_registration.json           # 新增命令的 checklist
│   └── error_recovery.json                 # 错误恢复策略
│
├── execution_history/                      # Layer 3: 动态知识
│   └── recent_operations.jsonl             # 最近操作记录
│
├── project_state/                          # 项目运行时状态
│   ├── known_materials.json                # 已知材质列表
│   ├── known_assets.json                   # 已知资产目录
│   └── current_session.json                # 当前会话状态
│
└── index.json                              # 简易倒排索引
```

---

## 四、数据结构设计

### 4.1 统一知识条目 Schema

所有知识条目遵循统一格式（跨所有阶段不变）：

```python
@dataclass
class KnowledgeEntry:
    id: str                    # 唯一ID，如 "mat_expr_custom_001"
    category: str              # 大类：api_reference / lessons_learned / best_practices
    title: str                 # 简明标题
    content: str               # 详细内容（Markdown 格式）
    tags: list[str]            # 标签列表
    ue_version: str = None     # 适用的 UE 版本
    confidence: float = 1.0    # 可信度（0~1），AI 自动写入的默认 0.8
    source: str = "manual"     # 来源：manual / ai_generated / imported
    created_at: str = None     # 创建时间
    updated_at: str = None     # 更新时间
    access_count: int = 0      # 被查询次数（热度追踪）
```

### 4.2 知识条目示例

#### 材质表达式 API 参考

```json
{
  "MaterialExpressionCustom": {
    "description": "自定义 HLSL 代码节点",
    "outputs": [{"name": "", "type": "depends on OutputType"}],
    "inputs": [{"name": "动态，通过 InputName[N] 设置", "type": "any"}],
    "settable_properties": {
      "Code": {"type": "string", "description": "HLSL 代码"},
      "Description": {"type": "string", "description": "节点标题"},
      "OutputType": {
        "type": "enum",
        "values": ["CMOT_Float1", "CMOT_Float2", "CMOT_Float3", "CMOT_Float4", "CMOT_MaterialAttributes"]
      },
      "InputName[N]": {"type": "string", "description": "第 N 个输入的名称，HLSL 中必须用此名称引用"}
    },
    "important_notes": [
      "必须先连接输入节点，再设置 InputName",
      "HLSL 代码中的变量名必须和 InputName 完全一致",
      "操作顺序: 创建 → 连接输入 → 设 InputName → 设 Code → 设 OutputType → 重编译"
    ]
  }
}
```

#### 经验知识条目

```json
{
  "id": "lesson_ue57_api_changes",
  "category": "lessons_learned",
  "title": "UE 5.7 API 破坏性变更",
  "content": "GetExpressions() 返回 TArrayView 而非 TArray&，用 auto 接收。ANY_PACKAGE_COMPAT 已移除，改用 FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst)",
  "tags": ["ue57", "api-breaking-change", "compilation"],
  "ue_version": "5.7",
  "confidence": 1.0,
  "source": "manual"
}
```

### 4.3 `set_expression_value` 属性速查表

| 节点类型 | property_name | value 格式 |
|---------|---------------|------------|
| Constant | `R` | 浮点数 `"1.0"` |
| Constant3Vector | `Constant` | JSON `{"r":1,"g":0,"b":0,"a":1}` |
| Constant4Vector | `Constant` | JSON `{"r":1,"g":0,"b":0,"a":1}` |
| ScalarParameter | `ParameterName` | 字符串 `"MyParam"` |
| ScalarParameter | `DefaultValue` | 浮点数 `"0.5"` |
| VectorParameter | `ParameterName` | 字符串 `"BaseColor"` |
| VectorParameter | `DefaultValue` | JSON `{"r":0.1,"g":0.3,"b":0.8,"a":1}` |
| TextureSample | `texture_path` | 资产路径 `"/Game/Textures/T_Wood"` |
| Custom | `Code` | HLSL 代码字符串 |
| Custom | `Description` | 节点标题字符串 |
| Custom | `OutputType` | 枚举 `"CMOT_Float3"` |
| Custom | `InputName[N]` | 输入名 `"Time"` |

### 4.4 材质属性枚举映射

| 属性 | 枚举值 |
|------|--------|
| BaseColor | `MP_BaseColor` |
| Metallic | `MP_Metallic` |
| Specular | `MP_Specular` |
| Roughness | `MP_Roughness` |
| Normal | `MP_Normal` |
| EmissiveColor | `MP_EmissiveColor` |
| Opacity | `MP_Opacity` |
| OpacityMask | `MP_OpacityMask` |
| AmbientOcclusion | `MP_AmbientOcclusion` |

---

## 五、渐进式扩展方案

### 5.1 阶段预估

| 阶段 | 条目量 | 典型内容 | 瓶颈 |
|------|--------|---------|------|
| **初期** | < 500 条 | UE API 参考、踩坑记录、工作流 | 无 |
| **中期** | 500 ~ 5,000 条 | 多项目经验、完整引擎 API 覆盖、用户自定义规则 | JSON 全文扫描变慢 |
| **后期** | 5,000 ~ 50,000+ 条 | 社区共享知识、多引擎版本、操作历史归档 | 需要语义检索、索引管理 |

### 5.2 Phase 1：JSON 文件（< 500 条）

**存储方式**：
- 静态知识 → JSON 文件，按类别分文件
- 经验知识 → JSONL（一行一条记录）
- 倒排索引 → `index.json`，维护 `keyword → [entry_id]` 映射

**检索方式**：
- 精确匹配：按 `category` + `tags` 过滤
- 模糊搜索：内存中对 `title` + `content` 做 substring/regex 匹配

**优点**：零依赖、可 git 管理、人可读可编辑  
**局限**：5000 条以上会明显变慢

```json
// index.json 示例
{
  "material": ["entry_001", "entry_005", "entry_012"],
  "compilation": ["entry_003", "entry_007"],
  "custom_node": ["entry_002", "entry_008"],
  "hlsl": ["entry_002", "entry_010"]
}
```

### 5.3 Phase 2：SQLite + FTS5（500 ~ 5,000 条）

**触发条件**：检索延迟 > 100ms 或条目 > 500

```sql
-- 主表
CREATE TABLE knowledge (
    id TEXT PRIMARY KEY,
    category TEXT NOT NULL,
    title TEXT NOT NULL,
    content TEXT NOT NULL,
    tags TEXT,                          -- JSON 数组
    ue_version TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    access_count INTEGER DEFAULT 0,
    confidence REAL DEFAULT 1.0
);

-- FTS5 全文搜索虚拟表
CREATE VIRTUAL TABLE knowledge_fts USING fts5(
    title, content, tags,
    content='knowledge',
    content_rowid='rowid',
    tokenize='porter unicode61'
);

-- 索引
CREATE INDEX idx_category ON knowledge(category);
CREATE INDEX idx_ue_version ON knowledge(ue_version);
```

**为什么 SQLite + FTS5 而不是直接上向量数据库**：

| 维度 | SQLite + FTS5 | ChromaDB / Qdrant |
|------|---------------|-------------------|
| 依赖 | **零外部依赖**（Python 内置） | 需要额外库/服务 |
| 部署 | 单文件，随项目走 | 需要启动独立进程 |
| 适合场景 | **结构化知识为主** | 自然语言描述为主 |
| UE 知识特点 | 大量精确名称匹配 | 少量模糊语义匹配 |
| 查询延迟 | < 1ms（万条级别） | 2~10ms |

> **核心洞察**：UE Agent 的知识 80% 是结构化的（类名、属性名、枚举值），FTS5 的关键词匹配比语义检索更精准。

### 5.4 Phase 3：SQLite + 向量引擎（5,000+ 条）

**触发条件**：非结构化经验描述增多，语义搜索需求明显

**混合检索架构**：

```
用户查询 → 查询路由
              ├─ 精确匹配（API名称/枚举值）→ SQLite FTS5
              ├─ 模糊描述（"材质怎么发光"）  → 向量引擎
              └─ 默认                        → 混合检索
           → 结果合并 & 重排序 → Top-K 结果
```

**推荐方案：sqlite-vec**

保持 SQLite 单文件架构，零额外依赖，向量搜索作为 SQLite 扩展运行：

```sql
CREATE VIRTUAL TABLE knowledge_vec USING vec0(
    id TEXT PRIMARY KEY,
    embedding FLOAT[384]    -- 384 维嵌入向量
);
```

**嵌入模型选择**：

| 模型 | 维度 | 大小 | 推荐场景 |
|------|------|------|---------|
| all-MiniLM-L6-v2 | 384 | 80MB | 本地部署首选 |
| bge-small-zh-v1.5 | 512 | 96MB | 中文知识为主 |
| text-embedding-3-small | 1536 | API | 有网络、追求质量 |

**智能查询路由**：

```python
class QueryRouter:
    def route(self, query: str) -> str:
        # 包含 UE 类名/枚举值 → 精确匹配
        if re.match(r'(MaterialExpression|EMaterial|UMaterial)', query):
            return "keyword"
        # 包含 "怎么"/"如何" → 语义检索
        if any(kw in query for kw in ["怎么", "如何", "为什么", "how", "why"]):
            return "semantic"
        return "hybrid"
```

---

## 六、检索工具设计

### 6.1 `query_knowledge` — 查询知识库

```python
@tool
async def query_knowledge(
    category: str,        # "api_reference" | "lessons_learned" | "best_practices" | "project_state"
    query: str,           # 搜索关键词或问题
    tags: list[str] = []  # 可选标签过滤
) -> dict:
    """AI 在执行操作前查询相关知识，避免重复踩坑。
    
    使用场景：
    - 创建材质节点前 → 查询该节点的属性名和输入输出
    - 编译代码前 → 查询已知的 UE 5.7 API 变更
    - 执行新操作前 → 查询相关最佳实践
    """
```

### 6.2 `save_knowledge` — 写入经验

```python
@tool
async def save_knowledge(
    category: str,        # "lessons_learned" | "project_state"
    title: str,
    content: str,
    tags: list[str] = []
) -> dict:
    """AI 在遇到新问题并解决后，将经验存入知识库供后续复用。"""
```

### 6.3 检索接口抽象层

```python
class KnowledgeStore(ABC):
    """抽象接口 —— 存储层可替换，上层代码不需要改"""
    
    @abstractmethod
    def search(self, query: str, category: str = None,
               tags: list[str] = None, top_k: int = 5) -> list[KnowledgeEntry]: ...
    
    @abstractmethod
    def save(self, entry: KnowledgeEntry) -> str: ...
    
    @abstractmethod
    def update(self, entry_id: str, updates: dict) -> bool: ...
    
    @abstractmethod
    def delete(self, entry_id: str) -> bool: ...

# Phase 1
class JsonKnowledgeStore(KnowledgeStore): ...

# Phase 2
class SqliteKnowledgeStore(KnowledgeStore): ...

# Phase 3
class HybridKnowledgeStore(KnowledgeStore): ...
```

---

## 七、知识生命周期管理

### 7.1 生命周期流程

```
新增知识 (confidence=0.8) → 候选区
    ├─ 人工审核通过 → 正式区 (confidence=1.0)
    ├─ 30天未查询   → 归档区
    └─ UE版本过期   → 废弃标记 → 确认后删除
```

### 7.2 自动维护策略

```python
class KnowledgeLifecycle:
    def auto_archive(self, days_unused=90):
        """将超过 90 天未被查询的知识移入归档"""
        
    def auto_deprecate(self, old_ue_version: str):
        """标记旧版本 UE 的知识为 deprecated"""
        
    def deduplicate(self):
        """检测并合并重复/相似知识条目"""
        
    def quality_audit(self):
        """定期检查 confidence < 0.5 的条目，提示人工 review"""
```

---

## 八、System Prompt 集成

在 Agent 的 System Prompt 中加入知识库使用指引：

```
你是 UnrealAgent，一个 UE5 编辑器 AI 助手。

## 知识库使用规则
1. 在执行任何材质编辑操作前，先用 query_knowledge(category="api_reference") 查询相关节点类型
2. 遇到编译错误时，先查询 query_knowledge(category="lessons_learned", tags=["compilation"])
3. 成功解决新问题后，用 save_knowledge 记录经验
4. 不要猜测 API 用法，不确定时先查知识库
```

---

## 九、已积累的经验知识（首批入库内容）

以下是本次任务中积累的经验，作为知识库的首批内容：

### 9.1 UE 5.7 API 变更

| 旧写法 | UE 5.7 正确写法 | 原因 |
|--------|-----------------|------|
| `const TArray<...>& Exprs = Mat->GetExpressions()` | `auto Exprs = Mat->GetExpressions()` | 返回 `TArrayView` 非 `TArray&` |
| `FindObject<UClass>(ANY_PACKAGE_COMPAT, *Name)` | `FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst)` | `ANY_PACKAGE_COMPAT` 已移除 |

### 9.2 Custom HLSL 节点操作顺序

1. 创建 `MaterialExpressionCustom` 节点
2. 先连接输入（如 Time → Custom），让 `Inputs` 数组被填充
3. 设置 `InputName[0]` = HLSL 代码中使用的变量名
4. 设置 `Code`（HLSL 代码）
5. 设置 `OutputType`（`CMOT_Float3` 等）
6. 重编译材质

> **关键**：HLSL 代码中的变量名必须和 `InputName` 完全一致，否则编译报 `undeclared identifier`

### 9.3 编译命令（Git Bash 环境）

```bash
# 不能用 Build.bat（Git Bash 空格路径问题），直接调用 UnrealBuildTool.exe
'/c/Program Files/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' \
  AuraEditor Win64 Development \
  -Project='I:/Aura/Aura.uproject' \
  -WaitMutex -FromMsBuild
```

### 9.4 热加载局限性

- UE 5.7 热加载（Live Coding）在某些情况下不会替换已加载的模块
- 新增/修改函数逻辑后，**必须重启 UE 编辑器**才能确保新 DLL 生效
- 验证方法：看 DLL 后缀递增（如 `-0001` → `-0002`），但递增不保证运行时加载了新版本

### 9.5 测试脚本注意事项

- Windows 下使用 emoji 需要 `python -X utf8 test_xxx.py`
- JSON-RPC 通信协议：`Content-Length: {字节数}\r\n\r\n{JSON body}`

### 9.6 新增命令 Checklist（4 文件）

| 文件 | 操作 |
|------|------|
| `Public/Commands/UAXxxCommands.h` | 新建头文件，声明命令类 |
| `Private/Commands/UAXxxCommands.cpp` | 新建实现文件 |
| `UnrealAgent.Build.cs` | 添加所需模块依赖 |
| `Private/Commands/UACommandRegistry.cpp` | 添加 `#include` 和 `RegisterCommand(...)` |

---

## 十、实施路线

| 阶段 | 内容 | 预估时间 |
|------|------|---------|
| **Phase 1** | 创建知识库目录、编写核心 JSON、实现 query/save 工具 | 3~4 天 |
| **Phase 2** | 填充材质表达式大全、UE 5.7 踩坑记录、操作工作流 | 2~3 天 |
| **Phase 3** | System Prompt 集成、端到端测试 | 1~2 天 |
| **Phase 2 迁移** | 条目 > 500 时迁移到 SQLite + FTS5 | 按需 |
| **Phase 3 迁移** | 条目 > 5000 时引入 sqlite-vec 向量检索 | 按需 |
