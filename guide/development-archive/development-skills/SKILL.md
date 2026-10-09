---
name: cus3d-interface-audit
description: Perform interface and struct incompatibility audit for star-3d (s3d) Embree-to-CuBQL backend migration. Use when user requests (1) auditing interface compatibility, (2) checking type mismatches, (3) analyzing API dependencies during Embree to CuBQL migration, or (4) preparing for backend migration. MUST EXCLUSIVELY use LSP tools (lsp_diagnostics, lsp_symbols, lsp_find_references) for ALL type analysis and deepwiki-mcp MCP tools (deepwiki-mcp_read_wiki_contents, deepwiki-mcp_ask_question) for documentation lookup from EricSolshkov/Embree and EricSolshkov/cuBQL repositories. MAY delegate subtasks to librarian/explore/oracle agents BUT delegated agents MUST follow the SAME tool restrictions (LSP + deepwiki-mcp ONLY, NO grep/websearch/webfetch). ABSOLUTELY FORBIDDEN: grep, ast-grep, websearch, webfetch, or any non-LSP analysis tools. CRITICAL: Violating tool restrictions will produce INCORRECT, INCOMPLETE audit results that will DERAIL the migration project. If LSP is unavailable, REFUSE to proceed - stopping is better than producing flawed audits.
---

# CUS3D Interface Incompatibility Audit

Comprehensive interface and structure incompatibility audit workflow for migrating star-3d project from Embree to CuBQL backend.

## Overview

This skill performs systematic audits of header files (*.h) to identify all Embree dependencies that need CuBQL migration. It ensures complete documentation of type mismatches, missing definitions, and generates detailed migration recommendations.

**Audit Scope**: Header files (*.h) ONLY - source files (*.c/*.cpp) excluded from direct analysis

**Key Outputs**: 
- Complete file structure analysis
- Per-file incompatibility details
- Embree API role documentation
- CuBQL equivalent mappings
- Migration strategy recommendations

## Prerequisites Check (CRITICAL - MUST EXECUTE FIRST)

Before starting audit, verify LSP availability:

```bash
# Test LSP diagnostics on any s3d header file
lsp_diagnostics(filePath="<s3d-project-path>/include/<any>.h")
```

**IF LSP UNAVAILABLE**: STOP immediately. Display error:
```
🚫 AUDIT BLOCKED: LSP tools unavailable

Required Tools:
  - lsp_diagnostics
  - lsp_symbols  
  - lsp_find_references

REASON: Type analysis REQUIRES LSP. No fallback tools permitted.
ACTION: Configure C/C++ language server (clangd/Microsoft C++) before retry.

⚠️ CRITICAL: Proceeding without LSP will produce FLAWED audit results.
   Incomplete/incorrect audits will DERAIL the migration project.
   STOPPING is better than producing unreliable data.
   DO NOT attempt workarounds with grep/ast-grep/other tools.
```

**IF LSP AVAILABLE**: Proceed to workflow.

**CRITICAL CONSTRAINT - VIOLATION CONSEQUENCES**: 

⚠️ **WHY THESE RESTRICTIONS EXIST**:
- Using grep instead of LSP → Misses type relationships, produces incomplete symbol lists
- Using webfetch instead of deepwiki-mcp → Wrong repository, outdated documentation
- Using websearch instead of deepwiki-mcp → Unreliable sources, version mismatches
- **RESULT**: Incorrect migration mappings → Failed CuBQL integration → Project setback

⚠️ **WHAT HAPPENS IF YOU VIOLATE**:
1. Audit report will contain INCORRECT type mappings
2. Migration team implements WRONG CuBQL equivalents
3. Integration fails, requiring COMPLETE AUDIT REDO
4. Project timeline DERAILS, wasted engineering effort
5. **IT IS BETTER TO STOP AND REPORT LIMITATIONS THAN PRODUCE FLAWED RESULTS**

**Throughout the entire audit workflow, you are ABSOLUTELY FORBIDDEN from using**:
- ❌ grep (text search) - will miss LSP type info
- ❌ ast-grep (AST pattern matching) - incomplete type resolution
- ❌ websearch (web search) - unreliable documentation sources
- ❌ webfetch (HTTP fetching) - wrong repository versions
- ❌ Manual file reading for type analysis - no type system awareness
- ❌ Any tool other than LSP for type/symbol analysis

**ONLY PERMITTED NON-LSP TOOLS**: 
- ✅ deepwiki-mcp MCP tools (deepwiki-mcp_read_wiki_contents, deepwiki-mcp_ask_question)
- ✅ Use ONLY with repositories: EricSolshkov/Embree and EricSolshkov/cuBQL
- ✅ glob for file discovery
- ✅ read for usage context AFTER LSP locates symbol

## Workflow

### Phase 1: Project Structure Analysis

1. **Scan all header files** in s3d project (`stardis-cpu/star-3d/0.10/`):
   ```bash
   glob(pattern="**/*.h", path="<s3d-project-path>")
   ```

2. **Document file organization**:
   - Full directory tree
   - Each file's role (API, data structures, internal helpers)
   - File dependency relationships
   - Public vs internal interfaces

3. **Create structure section** in `guide/cus3d/interface_incompetibility.md`:
   ```markdown
   ## Project File Structure
   
   ### Directory Layout
   [Complete tree structure]
   
   ### File Roles
   | File | Role | Public API | Dependencies |
   |------|------|-----------|--------------|
   | xxx.h | ... | Yes/No | ... |
   ```

### Phase 1.5: Agent Delegation Strategy (OPTIONAL - If Needed)

**When to Consider Delegation**:
- Large number of header files (>20) → parallel LSP analysis
- Multiple Embree types need documentation → parallel deepwiki-mcp queries
- Complex dependency chains → explore agent for relationship mapping

**CRITICAL DELEGATION REQUIREMENTS**:

Every delegation MUST include explicit tool restrictions in the prompt:

```
delegate_task(
  subagent_type="<agent-type>",
  prompt="[Task description]
  
  MANDATORY TOOL RESTRICTIONS:
  - Type Analysis: ONLY lsp_diagnostics, lsp_symbols, lsp_find_references
  - Documentation: ONLY deepwiki-mcp_read_wiki_contents, deepwiki-mcp_ask_question
  - Repositories: ONLY EricSolshkov/Embree and EricSolshkov/cuBQL
  - ABSOLUTELY FORBIDDEN: grep, ast-grep, websearch, webfetch, any text search
  - If LSP/deepwiki-mcp unavailable: STOP and report limitation, do NOT use alternatives
  
  [Rest of task instructions]",
  run_in_background=true/false
)
```

**Agent-Specific Delegation Examples**:

**Librarian Agent** (for documentation queries):
```
delegate_task(
  subagent_type="librarian",
  prompt="Query documentation for RTCScene from EricSolshkov/Embree repository.
  
  TOOL RESTRICTIONS:
  - Use ONLY: deepwiki-mcp_ask_question(repoName='EricSolshkov/Embree', question='...')
  - FORBIDDEN: websearch, webfetch, grep, any web browsing
  - If deepwiki-mcp fails: Return 'Documentation unavailable', do NOT try alternatives
  
  Question: What is RTCScene and what are its key characteristics?",
  run_in_background=true
)
```

**Explore Agent** (for codebase structure):
```
delegate_task(
  subagent_type="explore",
  prompt="Find all header files using RTCDevice type.
  
  TOOL RESTRICTIONS:
  - Use ONLY: lsp_symbols, lsp_find_references for symbol tracking
  - Use ONLY: glob for file discovery
  - FORBIDDEN: grep, ast-grep, any text search
  - If LSP fails for a symbol: Document 'LSP resolution failed', do NOT use grep
  
  Task: Locate all usages of RTCDevice type across header files.",
  run_in_background=true
)
```

**Oracle Agent** (for complex analysis):
```
delegate_task(
  subagent_type="oracle",
  prompt="Analyze type dependency chain for s3d_scene_t structure.
  
  TOOL RESTRICTIONS:
  - Use ONLY: lsp_symbols, lsp_find_references for type analysis
  - Use ONLY: read for viewing source after LSP locates symbols
  - FORBIDDEN: grep, ast-grep, manual code search
  - If LSP cannot resolve: Report limitation, do NOT guess
  
  Task: Map complete type dependency chain starting from s3d_scene_t.",
  run_in_background=false
)
```

**Verification After Delegation**:
1. Check agent response for mention of forbidden tools (grep, websearch)
2. If forbidden tool was used → DISCARD result, re-delegate with stronger restrictions
3. Document in audit report: "Used [agent] for [task] with tool restrictions enforced"

### Phase 2: Type Incompatibility Detection

For each header file:

1. **Run LSP diagnostics**:
   ```bash
   lsp_diagnostics(filePath="<header-file-path>", severity="error")
   ```

2. **Identify incompatibilities**:
   - Undefined types (Embree types missing)
   - Incomplete type errors
   - Undeclared identifiers
   - Missing function declarations
   - Macro definition errors

3. **Extract symbols**:
   ```bash
   lsp_symbols(filePath="<header-file-path>", scope="document")
   ```
   Document all structs, functions, typedefs using Embree types.

4. **Find all references** for each problematic symbol:
   ```bash
   lsp_find_references(filePath="<file>", line=<n>, character=<m>, includeDeclaration=true)
   ```
   Track usage across codebase.

5. **IF LSP symbol resolution fails**:
   - Document the failure in audit report
   - Mark symbol for manual investigation
   - Continue audit with available LSP data
   - **DO NOT use grep, ast-grep, or any fallback tool**
   - Add to "Symbols Requiring Manual Review" section

### Phase 3: Embree Role Documentation

For each missing Embree type/function:

1. **Analyze usage in source files** (*.c files):
   - Read implementation files that use the type
   - Document HOW the type/function is used
   - Identify the semantic role in s3d architecture

2. **Query Embree Documentation** (using deepwiki-mcp MCP tools):
   ```bash
   # Use deepwiki-mcp MCP tool with EXACT repository name
   deepwiki-mcp_read_wiki_contents(repoName="EricSolshkov/Embree")
   
   # Or for specific type/function questions:
   deepwiki-mcp_ask_question(
     repoName="EricSolshkov/Embree", 
     question="What is RTCScene and how is it used?"
   )
   ```
   
   ⚠️ **CRITICAL**: 
   - Repository MUST be "EricSolshkov/Embree" (NOT "RenderKit/embree")
   - Use deepwiki-mcp MCP tools ONLY (NOT webfetch/websearch)
   - Wrong repository = wrong documentation = incorrect audit
   
   Search for:
   - Type/function official documentation
   - Parameter semantics
   - Return value meanings
   - Typical usage patterns
   - Performance considerations

3. **Document Embree role** in audit report:
   ```markdown
   ### <Type/Function Name>
   
   **Missing Definition**: RTCScene (example)
   **Used In**: s3d_scene.h, s3d_raytracer.h
   **Usage Context**: Primary scene container for geometric data
   
   **Embree Role** (from Deepwiki):
   - Opaque handle to scene object
   - Contains geometry primitives for ray intersection
   - Thread-safe after rtcCommitScene()
   - Supports incremental updates
   
   **Implementation Details**:
   [Key technical details from Deepwiki]
   ```

### Phase 4: CuBQL Mapping Discovery

For each documented Embree dependency:

1. **Query CuBQL Documentation** (using deepwiki-mcp MCP tools):
   ```bash
   # Use deepwiki-mcp MCP tool with EXACT repository name
   deepwiki-mcp_read_wiki_contents(repoName="EricSolshkov/cuBQL")
   
   # Or for specific mapping questions:
   deepwiki-mcp_ask_question(
     repoName="EricSolshkov/cuBQL", 
     question="What is the CuBQL equivalent of Embree's RTCScene?"
   )
   ```
   
   ⚠️ **CRITICAL**: 
   - Repository MUST be "EricSolshkov/cuBQL" (NOT "nvidia/cubql")
   - Use deepwiki-mcp MCP tools ONLY (NOT webfetch/websearch)
   - Wrong repository = wrong mappings = migration failure
   
   Search for equivalent concepts:
   - Similar data structures
   - Equivalent functions
   - Alternative APIs with same role
   - Implementation patterns

2. **Document CuBQL equivalent** if found:
   ```markdown
   **CuBQL Equivalent**: cubqlScene_t (example)
   **Mapping Confidence**: High/Medium/Low
   **API Differences**:
   - Creation: cubqlCreateScene() vs rtcNewScene()
   - Thread model: [differences]
   - Memory management: [differences]
   
   **Migration Notes**:
   [Specific adaptation requirements]
   ```

3. **Document alternative approach** if NO direct equivalent:
   ```markdown
   **CuBQL Equivalent**: None (direct)
   **Recommended Approach**: Custom wrapper around cubqlBVH_t + cubqlGeometry_t
   **Rationale**: CuBQL uses lower-level primitives; requires abstraction layer
   
   **Implementation Strategy**:
   1. Create s3d_cubql_scene wrapper struct
   2. Encapsulate BVH + geometry collections
   3. Provide RTCScene-like API for compatibility
   
   **CuBQL APIs Involved**:
   - cubqlCreateBVH()
   - cubqlBuildBVH()
   - cubqlTraverseRays()
   ```

### Phase 5: Report Generation

Compile all findings into `guide/cus3d/interface_incompetibility.md`:

**Report Structure** (MANDATORY):

```markdown
# Star-3D Embree to CuBQL Interface Incompatibility Audit

**Generated**: [timestamp]
**Audit Scope**: Header files (*.h) in stardis-cpu/star-3d/0.10/
**Tool Chain**: LSP diagnostics + deepwiki-mcp (Embree/CuBQL)

---

## Executive Summary

- Total header files analyzed: N
- Files with incompatibilities: M
- Unique Embree types requiring migration: X
- Direct CuBQL equivalents found: Y
- Custom implementations needed: Z

---

## Project File Structure

[Complete directory tree and file roles from Phase 1]

---

## Incompatibility Details

[For each file with issues:]

### File: <path/to/header.h>

**Role**: [File purpose and API category]
**Dependencies**: [Other headers this depends on]

#### Incompatibility 1: [Missing Type/Function]

**Missing Definition**: `RTCScene` (example)
**Error Context**: 
- Struct `s3d_scene_t` member `RTCScene scene` at line 42
- Function `s3d_create_scene()` return type at line 89

**Usage in s3d**:
- Primary scene container
- Used in: s3d_scene.c, s3d_raytracer.c, s3d_builder.c
- Public API exposure: Yes (s3d_scene.h)

**Embree Role** (from Deepwiki):
[Complete documentation from Phase 3]

**CuBQL Equivalent**:
[Mapping details from Phase 4]

**Migration Impact**: High/Medium/Low
**Migration Complexity**: High/Medium/Low

---

[Repeat for all incompatibilities]

---

## Migration Roadmap

### Phase 1: Direct Replacements (Low Risk)
[List items with direct CuBQL equivalents]

### Phase 2: Wrapper Implementations (Medium Risk)
[List items needing thin abstraction layers]

### Phase 3: Architectural Changes (High Risk)
[List items requiring significant redesign]

---

## Appendix: Tool Configuration

**LSP Server**: [C/C++ language server details]
**Diagnostics Coverage**: [Files successfully analyzed vs failed]
**Deepwiki Query Summary**: [Total queries, success rate]
```

## Tool Usage Rules (NON-NEGOTIABLE - STRICTLY ENFORCED)

### MUST USE - ONLY These Tools

**Type Analysis (MANDATORY - NO EXCEPTIONS)**:

1. **LSP Diagnostics** (`lsp_diagnostics`):
   - EXCLUSIVE tool for type error detection
   - Run on EVERY header file
   - Filter by severity="error"
   - Document ALL error messages
   - **NO fallback allowed if LSP fails**

2. **LSP Symbols** (`lsp_symbols`):
   - EXCLUSIVE tool for symbol extraction
   - Extract all struct/function definitions
   - Track symbol dependencies
   - Map symbol usage patterns
   - **NO fallback allowed if LSP fails**

3. **LSP Find References** (`lsp_find_references`):
   - EXCLUSIVE tool for cross-reference tracking
   - Track usage of problematic symbols
   - Build cross-file dependency map
   - Identify public API surface
   - **NO fallback allowed if LSP fails**

**Documentation Queries (MANDATORY - NO EXCEPTIONS)**:

4. **deepwiki-mcp MCP tools** (ONLY these specific tools):
   - EXCLUSIVE tools for Embree/CuBQL documentation
   - **Tool 1**: `deepwiki-mcp_read_wiki_contents(repoName="EricSolshkov/Embree")` or `repoName="EricSolshkov/cuBQL"`
   - **Tool 2**: `deepwiki-mcp_ask_question(repoName="EricSolshkov/Embree", question="...")` or `repoName="EricSolshkov/cuBQL"`
   - **CRITICAL**: Repository names MUST be EXACTLY "EricSolshkov/Embree" and "EricSolshkov/cuBQL"
   - Extract role documentation from these repositories ONLY
   - Find equivalent APIs from these repositories ONLY
   - **ABSOLUTELY NO webfetch, websearch, or web browsing allowed**
   - **Wrong repository = incorrect documentation = failed migration**

**File System Operations (PERMITTED)**:

5. **Glob** (`glob`):
   - For discovering header files only
   - Pattern: `**/*.h`

6. **Read** (`read`):
   - For reading source files to understand symbol usage context
   - ONLY after LSP identifies the symbol location
   - NOT for type analysis

**Agent Delegation (CONDITIONALLY PERMITTED)**:

7. **Librarian / Explore / Oracle Agents** (delegate_task):
   - ✅ **ALLOWED** for complex subtasks (multi-file analysis, parallel queries)
   - ⚠️ **CRITICAL REQUIREMENT**: Delegated agents MUST be explicitly instructed to follow the SAME tool restrictions
   - ✅ **Must include in delegation prompt**:
     ```
     TOOL RESTRICTIONS (MANDATORY):
     - ONLY use LSP tools: lsp_diagnostics, lsp_symbols, lsp_find_references
     - ONLY use deepwiki-mcp: deepwiki-mcp_read_wiki_contents, deepwiki-mcp_ask_question
     - Repositories: EricSolshkov/Embree, EricSolshkov/cuBQL ONLY
     - FORBIDDEN: grep, ast-grep, websearch, webfetch
     - If tool unavailable: document gap, do NOT use alternatives
     ```
   - ❌ **NEVER delegate without explicit tool restrictions** - agent will use unrestricted tools
   - ✅ **Example valid delegation**:
     ```
     delegate_task(
       subagent_type="librarian",
       prompt="Query EricSolshkov/Embree documentation for RTCScene.
               MANDATORY TOOL RESTRICTIONS:
               - Use ONLY deepwiki-mcp_ask_question(repoName='EricSolshkov/Embree')
               - FORBIDDEN: websearch, webfetch, any other tools
               - Document if deepwiki-mcp fails, do NOT use alternatives",
       run_in_background=true
     )
     ```

### ABSOLUTELY FORBIDDEN - VIOLATION = AUDIT FAILURE

⚠️ **UNDERSTAND THE CONSEQUENCES BEFORE VIOLATING**:

Using forbidden tools will NOT produce "good enough" results. It will produce WRONG results that:
1. ❌ Mislead the migration team with incorrect type mappings
2. ❌ Cause CuBQL integration failures requiring COMPLETE REDO
3. ❌ Waste weeks of engineering time on wrong implementation paths
4. ❌ Delay project timeline by requiring audit from scratch
5. ❌ Damage trust in AI-assisted analysis tools

**THE HARSH TRUTH**: 
- A partial audit with documented gaps is USEFUL
- A complete audit with subtle errors is DESTRUCTIVE
- **Stopping beats producing flawed data that derails engineering work**

---

**NEVER use these tools under ANY circumstances**:

| Tool | Why Forbidden | What Happens If You Use It |
|------|---------------|----------------------------|
| ❌ **grep** / **ast-grep** | No type system awareness | Misses type relationships → incomplete symbol lists → wrong migration plan |
| ❌ **webfetch** | Wrong repositories, no version control | Outdated docs → incorrect API usage → integration failures |
| ❌ **websearch** | Random web sources, unreliable | Version mismatches → wrong CuBQL mappings → runtime crashes |
| ❌ **Manual code inspection** | Cannot resolve cross-file type dependencies | Incomplete type chains → broken migration |
| ❌ **Librarian agent** (without restrictions) | May use websearch internally | Unreliable documentation sources → incorrect guidance |
| ❌ **Oracle agent** (without restrictions) | May use unrestricted tools | Violates tool restriction principle |
| ❌ **Explore agent** (without restrictions) | May use text search, not LSP | Misses type semantics → incomplete analysis |
| ⚠️ **Agents WITH restrictions** | Allowed if explicitly restricted | See "Agent Delegation Rules" below |
| ❌ **Any text search** | Pattern matching != type analysis | False positives/negatives → unreliable audit |

**If you use forbidden tools**: The audit report will look complete but contain subtle errors that won't be caught until CuBQL integration fails in testing or production.

---

### When LSP/deepwiki-mcp Fails

**If LSP symbol resolution fails**:
1. ✅ Document in "Symbols Requiring Manual Review" section with full context
2. ✅ Continue audit with available LSP data from other files
3. ✅ Mark for post-audit manual investigation by human engineer
4. ✅ Be honest about gaps: "LSP could not resolve symbol X, manual inspection needed"
5. ❌ **NEVER attempt workarounds with grep/ast-grep/other tools**
6. ❌ **NEVER pretend you found complete information when you didn't**

**If deepwiki-mcp query fails**:
1. ✅ Document as "Documentation unavailable from EricSolshkov/Embree"
2. ✅ Note in audit report's "Manual Review Required" section
3. ✅ Explain which types need manual Embree/CuBQL docs review
4. ✅ Be transparent: audit is incomplete without this documentation
5. ❌ **NEVER attempt workarounds with websearch/webfetch/other sources**
6. ❌ **NEVER fill gaps with speculation or unofficial documentation**

---

### Enforcement - STOP and THINK

**If you find yourself considering ANY forbidden tool, ask yourself**:

1. 🤔 "Am I about to use grep/websearch because LSP/deepwiki-mcp 'failed'?"
   - ➡️ STOP. Document the gap honestly instead.

2. 🤔 "Will this workaround give me 'close enough' information?"
   - ➡️ NO. Close enough = wrong enough. Engineers will implement based on your output.

3. 🤔 "But I need SOME answer to complete the audit..."
   - ➡️ WRONG MINDSET. Incomplete audit with gaps documented > complete audit with errors.

4. 🤔 "Maybe just this once, for this one symbol..."
   - ➡️ NO EXCEPTIONS. One wrong mapping can break the entire migration.

**If delegating to agents**:

5. 🤔 "Am I delegating with explicit tool restrictions?"
   - ➡️ YES REQUIRED. Include tool restrictions in delegation prompt.
   - ➡️ Agents without restrictions will use forbidden tools (grep/websearch).

6. 🤔 "Can I trust the agent to follow restrictions without explicit instructions?"
   - ➡️ NO. Agents are stateless. Must explicitly specify restrictions in EVERY delegation.

**THE RULE**: Use ONLY LSP + deepwiki-mcp (EricSolshkov repos). If delegating, EXPLICITLY restrict agents to same tools. Document limitations honestly. NEVER circumvent restrictions.

## Error Handling

### LSP Unavailable
```
ERROR: LSP tools not available
STATUS: Audit ABORTED
REASON: Type analysis requires LSP diagnostics
ACTION: Configure C/C++ language server before retry
```

### Deepwiki Query Failure
```
WARNING: deepwiki-mcp query failed for <type>
NO FALLBACK: Document as "Documentation unavailable"
CONTINUE: Proceed with audit, mark for manual review
NOTE: "Migration guidance incomplete - manual Embree docs review required"
DO NOT: Use webfetch, websearch, or any alternative documentation source
```

### File Access Error
```
ERROR: Cannot access header file <path>
ACTION: Skip file, document in audit report
SECTION: Add to "Files Not Analyzed" appendix
```

## Validation Checklist

Before finalizing audit report, verify:

- [ ] LSP diagnostics run on ALL header files
- [ ] Every incompatibility has Embree role documentation (via deepwiki-mcp)
- [ ] Every incompatibility has CuBQL mapping attempt (via deepwiki-mcp)
- [ ] Report structure matches Phase 5 template
- [ ] Executive summary statistics accurate
- [ ] Migration roadmap categorized by complexity
- [ ] Tool usage rules followed (ONLY LSP + deepwiki-mcp used, NO grep/websearch/webfetch)
- [ ] If agents were used: all delegations included explicit tool restrictions
- [ ] Agent responses verified to have followed tool restrictions
- [ ] Report saved to `guide/cus3d/interface_incompetibility.md`
- [ ] No forbidden tools used (grep, ast-grep, webfetch, websearch, unrestricted agents)

## Post-Audit Actions

After generating report:

1. **DO NOT** modify any code
2. **DO NOT** implement migrations
3. **DO** notify user: "Audit complete. Report: guide/cus3d/interface_incompetibility.md"
4. **DO** summarize key findings:
   - Total incompatibilities found
   - Migration complexity assessment
   - Critical blockers identified
   - Recommended next steps

## References

- Audit methodology: `guide/cus3d/interface_incompetibility_audit.md`
- Star-3D source: `stardis-cpu/star-3d/0.10/`
- Embree docs: https://deepwiki.com/RenderKit/embree
- CuBQL docs: https://deepwiki.com/nvidia/cubql
