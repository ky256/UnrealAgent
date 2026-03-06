"""AST fingerprint extraction for Python code.

Extracts a structural fingerprint from Python code by:
1. Parsing the AST
2. Replacing all literals (str, int, float) with typed placeholders
3. Hashing the normalized structure

Same structure + different parameters → same fingerprint.
This is the foundation for Phase 2 pattern recognition.
"""

import ast
import hashlib
import logging
from typing import Any

logger = logging.getLogger(__name__)


class _LiteralReplacer(ast.NodeTransformer):
    """Replace all literal values with typed placeholders."""

    def __init__(self):
        self.str_count = 0
        self.num_count = 0
        self.parameters: dict[str, Any] = {}

    def visit_Constant(self, node: ast.Constant) -> ast.Constant:
        if isinstance(node.value, str):
            key = f"STR_{self.str_count}"
            self.parameters[key] = node.value
            self.str_count += 1
            node.value = f"{{{key}}}"
        elif isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
            key = f"NUM_{self.num_count}"
            self.parameters[key] = node.value
            self.num_count += 1
            node.value = f"{{{key}}}"
        return node

    # Python 3.7 compat — older AST uses Str/Num nodes
    def visit_Str(self, node: ast.Str) -> ast.Str:
        key = f"STR_{self.str_count}"
        self.parameters[key] = node.s
        self.str_count += 1
        node.s = f"{{{key}}}"
        return node

    def visit_Num(self, node: ast.Num) -> ast.Num:
        key = f"NUM_{self.num_count}"
        self.parameters[key] = node.n
        self.num_count += 1
        node.n = f"{{{key}}}"
        return node


def _count_nodes(tree: ast.AST) -> int:
    """Count total AST nodes."""
    return sum(1 for _ in ast.walk(tree))


def _normalize(tree: ast.AST) -> str:
    """Produce a normalized string representation of the AST."""
    return ast.dump(tree, annotate_fields=True, include_attributes=False)


def fingerprint(code: str) -> str | None:
    """Compute the structural fingerprint hash of Python code.

    Returns a 16-char hex hash, or None if the code cannot be parsed.
    """
    result = fingerprint_full(code)
    return result["fingerprint"] if result else None


def fingerprint_full(code: str) -> dict | None:
    """Compute full fingerprint data including template and parameters.

    Returns:
        {
            "fingerprint": "a3f8e21b12345678",   # 16-char hex hash
            "code_template": "...",                # Code with literals replaced
            "ast_node_count": 15,                  # Complexity metric
            "parameters_extracted": {"STR_0": "hello", "NUM_0": 42}
        }
        or None if the code cannot be parsed.
    """
    try:
        tree = ast.parse(code)
    except SyntaxError:
        logger.debug("Failed to parse code for fingerprinting")
        return None

    node_count = _count_nodes(tree)

    # Replace literals with placeholders
    replacer = _LiteralReplacer()
    modified_tree = replacer.visit(tree)
    ast.fix_missing_locations(modified_tree)

    # Generate template code (with placeholders)
    try:
        code_template = ast.unparse(modified_tree)
    except Exception:
        # ast.unparse may fail on some edge cases; fall back to dump
        code_template = _normalize(modified_tree)

    # Hash the normalized AST structure
    normalized = _normalize(modified_tree)
    fp_hash = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]

    return {
        "fingerprint": fp_hash,
        "code_template": code_template,
        "ast_node_count": node_count,
        "parameters_extracted": replacer.parameters,
    }
