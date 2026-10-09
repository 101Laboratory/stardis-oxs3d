# Tool Restriction Rules

## ⚠️ ABSOLUTE ENFORCEMENT

**Violations = Migration Failure**. These prevent: wrong symbols (grep misses macros/types), wrong docs (webfetch gets outdated versions), masked issues (fallbacks hide config problems), cost explosion (100 wrong functions vs 1 aborted session).

## Tool Rules

**Required Tools**:
- **Symbols**: LSP (`lsp_goto_definition`, `lsp_find_references`, `lsp_symbols`) - Compiler-grade resolution, type info, cross-file tracking
- **Docs**: Context7/deepwiki (`context7_query-docs`, `deepwiki-mcp_ask_question`) - Version-controlled, repo-specific
- **Files**: `read`, `edit`, `write`, `glob` - Safe, standard

**Forbidden Tools**:
- **Symbols**: grep, ast-grep, text search - Miss macros, includes, types
- **Docs**: webfetch, websearch, grep_app - Outdated, wrong versions
- **Workarounds**: Any fallback when required tool fails

## Failure Protocol

**First failure**: Mark function unavailable (`LSP_UNAVAILABLE`/`DOCS_UNAVAILABLE`), skip to next. NO workarounds.

**Second failure** (same tool, same session): ABORT migration immediately. Report config issue. User fixes before retry.

**Never**: Use grep when LSP fails, use webfetch when Context7 fails, continue after repeated failures, guess API behavior.

## Why Fallbacks Fail

**Scenario**: LSP misconfigured → agent uses grep fallback → 50 functions migrated with wrong symbols → compile passes → runtime crashes → debug cost 10x migration cost.

**Correct**: LSP fails → mark unavailable → skip → LSP fails again → ABORT → report issue → user fixes → restart with working tools → all functions correct.

**Principle**: Fail fast, fail safely. One wrong function early is better than 100 wrong functions late.

## Violation Examples

**❌ Wrong**: `if (lsp_failed) { result = grep(...) }` - Masks config issue, corrupts codebase
**✅ Right**: `if (lsp_failed) { mark_unavailable(); skip(); }` - Surfaces issue immediately

**❌ Wrong**: `if (docs_fail) { result = webfetch(...) }` - Gets wrong version, API misuse
**✅ Right**: `if (docs_fail) { mark_unavailable(); skip(); }` - Prevents bug injection

**❌ Wrong**: `if (second_lsp_fail) { try_alternative(); }` - Continues with broken tool
**✅ Right**: `if (second_lsp_fail) { ABORT(); report(); }` - Forces config fix

## Rationale Summary

**LSP-only for symbols**: Accurate (handles macros/templates), type-aware (distinguishes overloads), cross-file (resolves includes). Grep: text matching, no context.

**Context7/deepwiki-only for docs**: Version-controlled (exact repo commit), structured (AI-searchable), consistent (same source). Webfetch: generic/outdated, no version control.

**Fail-fast vs fallback**: Early detection (config issue visible immediately), cost control (1 abort << 100 wrong functions), quality (accurate results or none, not mixed).
