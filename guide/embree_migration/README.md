# Embree → cuBQL Migration Documentation Index

**Project**: STARDIS-GPU Embree Migration  
**Date**: 2026-01-23  
**Status**: 📋 PLANNING COMPLETE - READY FOR VALIDATION  

---

## Quick Start

**New to this project?** Start here:

1. Read [Executive Summary](#1-executive-summary) (this document, section below)
2. Review [Performance Comparison Plan](#2-performance-comparison-plan)
3. Check [Environment Requirements](#3-environment-requirements)
4. Consult [API Migration Mapping](#4-api-migration-mapping) when implementing

---

## Document Overview

### 1. Executive Summary
**File**: `migration_strategy_executive_summary.md`  
**Purpose**: High-level overview, decision matrix, risk assessment  
**Audience**: Project managers, stakeholders, decision makers  

**Key Contents**:
- ✅ GO/NO-GO recommendation: **CONDITIONAL GO** (pending Phase 0 validation)
- Technical feasibility assessment
- Expected performance gains: **1.8-2.0x overall** (10-100x on intersection)
- Timeline: 5-9 weeks full migration
- Resource requirements: 1 GPU engineer full-time
- Risk assessment and mitigation strategies

**Read this if**: You need to understand the business case and decide whether to proceed.

---

### 2. Performance Comparison Plan
**File**: `embree_cubql_performance_comparison_plan.md`  
**Purpose**: Detailed test plan for Phase 0 validation (standalone embree vs cuBQL comparison)  
**Audience**: Engineers implementing validation, QA testers  

**Key Contents**:
- Standalone C++/CUDA comparison program (NO stardis dependencies)
- Test scenarios: Cornell Box, sphere meshes, random triangles
- Embree wrapper implementation
- cuBQL wrapper implementation  
- Benchmark driver and metrics collection
- Validation framework (precision comparison)
- Expected vs actual performance analysis

**Deliverables**:
- Comparison program source code
- Performance benchmarks (CSV report)
- Precision validation results
- GO/NO-GO recommendation based on data

**Implementation Time**: 5-8 days

**Read this if**: You are implementing the validation phase or need to understand test methodology.

---

### 3. API Migration Mapping
**File**: `embree_to_cubql_api_migration_mapping.md`  
**Purpose**: Complete translation table for every Embree API call → cuBQL equivalent  
**Audience**: Engineers implementing the migration  

**Key Contents**:
- **Section 1-2**: Device and scene management (setup/teardown)
- **Section 3-4**: Geometry and buffer handling (data upload)
- **Section 5**: BVH build configuration (quality settings)
- **Section 6**: **Ray intersection queries** (THE BOTTLENECK - most critical)
- **Section 7-8**: Memory and error handling
- **Section 9**: Advanced features (custom geometry, dynamic BVH)
- **Section 10**: Complete migration checklist

**Critical Mappings**:
```cpp
// Embree (CPU, single ray)
rtcIntersect1(scene, &rayhit, NULL);

// cuBQL (GPU, batched)
cuBQL::shrinkingRadiusQuery::forEachPrim(lambda, bvh, origin, radius);
```

**Read this if**: You are writing code to replace Embree calls with cuBQL.

---

### 4. Environment Requirements
**File**: `environment_requirements.md`  
**Purpose**: Complete hardware/software setup specification  
**Audience**: DevOps, engineers setting up development environment  

**Key Contents**:
- Hardware requirements: RTX 3060+ GPU, 16GB RAM
- Software dependencies: CUDA 12.0+, Embree 4.0+, cuBQL
- Build tools: CMake 3.18+, GCC 11+ / MSVC 2019+
- Verification commands (check installation)
- Installation instructions (Windows & Linux)
- Troubleshooting guide

**Critical Info**:
- ✅ **NO STARDIS dependencies** needed for Phase 0
- ✅ All software is **free** (CUDA, Embree, cuBQL)
- ✅ Tested configurations provided

**Read this if**: You need to set up a development machine or CI/CD pipeline.

---

### 5. Embree Coupling Analysis
**File**: `embree_couple.md`  
**Purpose**: Original analysis of Embree usage in star-3d (call flow, data structures)  
**Audience**: Engineers understanding existing codebase  

**Key Contents**:
- Complete Embree integration architecture
- Call flow: device → scene → geometry → commit → intersect
- Data structures: RTCDevice, RTCScene, RTCGeometry, RTCRayHit
- 4 core files: s3d_device.c, s3d_scene_view.c, s3d_geometry.c, s3d_scene_view_trace_ray.c
- Performance bottleneck identification (50% time in rtcIntersect1)
- Memory layout and data flow

**Read this if**: You need to understand how Embree is currently used in STARDIS.

---

## Migration Workflow

### Phase 0: Validation (Week 1-2)
```
START
  ↓
1. Setup environment (environment_requirements.md)
  ↓
2. Implement standalone comparison (embree_cubql_performance_comparison_plan.md)
  ↓
3. Run benchmarks
  ↓
4. Analyze results
  ↓
5. GO/NO-GO decision
  ↓
  ├─→ [NO-GO] → STOP (explore alternatives)
  └─→ [GO] → Phase 1
```

### Phase 1: Core Migration (Week 3-5)
```
START
  ↓
1. Replace device management (api_migration_mapping.md §1)
  ↓
2. Replace scene/BVH creation (api_migration_mapping.md §2)
  ↓
3. Replace geometry setup (api_migration_mapping.md §3-4)
  ↓
4. **Replace intersection query** (api_migration_mapping.md §6) ← CRITICAL
  ↓
5. Integration testing
  ↓
Phase 2
```

### Phase 2: Optimization (Week 6-7)
- Batch size tuning
- Async CPU-GPU transfers
- Memory layout optimization
- Profiling and bottleneck elimination

### Phase 3: Validation & Deployment (Week 8-9)
- Full stardis integration tests
- Precision validation (1e-6 tolerance)
- Performance benchmarking
- Documentation updates

---

## Critical Path Timeline

```
Week 1-2:  [Phase 0: Validation] → GO/NO-GO Decision
             ↓ (if GO)
Week 3-5:  [Phase 1: Core Migration]
             ↓
Week 6-7:  [Phase 2: Optimization]
             ↓
Week 8-9:  [Phase 3: Testing & Deployment]
             ↓
          COMPLETE (1.8-2.0x speedup achieved)
```

---

## Decision Tree

```
Should we migrate Embree → cuBQL?
  ↓
[Check] Is Embree a bottleneck? (50% CPU time)
  ├─→ NO → Don't migrate
  └─→ YES → Continue
         ↓
[Check] Is GPU available? (RTX 3060+)
  ├─→ NO → Stay with Embree
  └─→ YES → Continue
         ↓
[Action] Implement Phase 0 validation
         ↓
[Check] Speedup ≥ 10x on intersection?
  ├─→ NO → Abort migration
  └─→ YES → Continue
         ↓
[Check] Precision ≤ 1e-5 error?
  ├─→ NO → Investigate (double precision, scaling)
  └─→ YES → **PROCEED TO FULL MIGRATION**
```

---

## Key Insights & Warnings

### ✅ Good News
1. **API compatibility confirmed** - all Embree calls have cuBQL equivalents
2. **Scope is controlled** - only star-3d library affected (~2K LOC)
3. **Low-risk validation exists** - standalone test before commitment
4. **Expected speedup is significant** - 10-100x on bottleneck operation

### ⚠️ Important Warnings
1. **Overall speedup limited by Amdahl's Law** - Maximum ~2x despite 50x GPU acceleration
2. **Batch processing required** - Cannot use single-ray API like Embree
3. **Explicit memory management** - No zero-copy shared buffers
4. **CPU-GPU transfer overhead** - Must batch ≥10K rays for efficiency

### 🔴 Showstoppers
1. **Phase 0 speedup < 5x** → Abort migration
2. **Precision errors > 1e-3** → Investigate root cause, may require double precision
3. **GPU memory insufficient** → Implement streaming/chunking

---

## Quick Reference

### Performance Expectations
| Metric | Baseline | Target | Stretch Goal |
|--------|----------|--------|--------------|
| BVH build time | 100 ms | 20 ms (5x) | 10 ms (10x) |
| 1M ray intersection | 500 ms | 10 ms (50x) | 5 ms (100x) |
| Overall application | 1.0x | **1.8x** | **2.0x** |

### Critical Numbers
- **Embree bottleneck**: 50% of CPU time
- **star-3d LOC**: ~2,000 lines affected
- **Migration time**: 5-9 weeks
- **Phase 0 time**: 1-2 weeks
- **Hardware cost**: $0 (use existing GPU)

### Contact & Support
- **Original analysis**: See `embree_couple.md` (generated 2026-01-21)
- **Dependency analysis**: See `../embree_dependency_scope_analysis_CRITICAL_UPDATE.md`
- **Type exposure check**: See `../embree_exposed_type_access.md`

---

## Appendix: File Modification History

| File | Created | Last Modified | Status |
|------|---------|---------------|--------|
| `embree_couple.md` | 2026-01-21 | 2026-01-21 | ✅ Complete |
| `embree_cubql_performance_comparison_plan.md` | 2026-01-23 | 2026-01-23 | ✅ Complete |
| `embree_to_cubql_api_migration_mapping.md` | 2026-01-23 | 2026-01-23 | ✅ Complete |
| `environment_requirements.md` | 2026-01-23 | 2026-01-23 | ✅ Complete |
| `migration_strategy_executive_summary.md` | 2026-01-23 | 2026-01-23 | ✅ Complete |
| `README.md` (this file) | 2026-01-23 | 2026-01-23 | ✅ Complete |

---

## Next Actions

### Immediate (This Week)
- [ ] **Review all documents** with team
- [ ] **Approve Phase 0 plan** (stakeholder sign-off)
- [ ] **Allocate resources** (1 GPU engineer, RTX 3060+ machine)
- [ ] **Begin Phase 0 implementation** (see performance comparison plan)

### Week 2
- [ ] **Complete Phase 0 validation**
- [ ] **Analyze benchmark results**
- [ ] **Make GO/NO-GO decision**

### If GO → Week 3+
- [ ] **Begin Phase 1 core migration**
- [ ] Follow detailed plan in migration mapping document

---

**Document Version**: 1.0  
**Authors**: Sisyphus (OhMyOpenCode - Ultrawork Mode)  
**Status**: 📋 DOCUMENTATION COMPLETE - AWAITING APPROVAL  
**Last Updated**: 2026-01-23  

---

## Questions?

**For technical questions**: Refer to the specific document (see links above)  
**For strategic questions**: See `migration_strategy_executive_summary.md`  
**For implementation questions**: See `embree_to_cubql_api_migration_mapping.md`  
**For setup questions**: See `environment_requirements.md`
