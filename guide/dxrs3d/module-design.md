# dxrstar-3d 模块详细设计

**版本**: 0.1  
**日期**: 2026-02-20  
**前置文档**: [architecture.md](architecture.md)

---

## 1. dxrs3d_device — D3D12 设备管理

### 1.1 对标: cus3d_device

| cus3d_device 字段 | dxrs3d_device 字段 | 说明 |
|------------------|-------------------|------|
| `cuda_device_id` | `adapter_index` | GPU 适配器索引 |
| `cudaStream_t stream` | `ID3D12CommandQueue* cmd_queue` | 直接命令队列 (COMPUTE) |
| `cudaStream_t transfer_stream` | `ID3D12CommandQueue* copy_queue` | 复制命令队列 |
| `total_mem` | `dedicated_video_memory` | 显存总量 |
| `sm_count` | 无直接等价 (通过 Feature Level 推断) | — |
| `max_threads_per_block` | 无 (固定 1024 per group) | — |

### 1.2 数据结构

```cpp
struct dxrs3d_device {
    /* D3D12 核心对象 */
    ID3D12Device5*              device;          /* DXR 要求 Device5 */
    IDXGIAdapter1*              adapter;         /* GPU 适配器 */
    ID3D12CommandQueue*         compute_queue;   /* 计算命令队列 */
    ID3D12CommandQueue*         copy_queue;      /* 复制命令队列 */
    ID3D12Fence*                fence;           /* 同步栅栏 */
    HANDLE                      fence_event;     /* 栅栏事件句柄 */
    UINT64                      fence_value;     /* 当前栅栏值 */

    /* 命令列表池 (按需分配，复用) */
    ID3D12CommandAllocator*     compute_alloc;   /* 计算命令分配器 */
    ID3D12GraphicsCommandList4* compute_cmdlist; /* 计算命令列表 */
    ID3D12CommandAllocator*     copy_alloc;      /* 复制命令分配器 */
    ID3D12GraphicsCommandList*  copy_cmdlist;    /* 复制命令列表 */

    /* 设备信息 */
    SIZE_T                      dedicated_video_memory;
    int                         dxr_tier;        /* 0=不支持, 1=DXR1.0, 2=DXR1.1 */
    int                         supports_inline_rt;  /* Inline RT 支持 */
    int                         supports_double;     /* 双精度支持 */

    /* Descriptor Heap (全局) */
    struct dxrs3d_descriptor_heap* cbv_srv_uav_heap;
    struct dxrs3d_descriptor_heap* sampler_heap;      /* 保留，暂不使用 */

    /* Shader / PSO 管理器 */
    struct dxrs3d_shader_mgr*   shader_mgr;
};
```

### 1.3 API 签名

```c
/* === 对标 cus3d_device API === */
res_T dxrs3d_device_create(int adapter_index, struct dxrs3d_device** out);
void  dxrs3d_device_destroy(struct dxrs3d_device* dev);
void  dxrs3d_device_sync(struct dxrs3d_device* dev);
const char* dxrs3d_get_last_error(void);

/* === DXR 特有 === */
int   dxrs3d_device_supports_dxr(struct dxrs3d_device* dev);
int   dxrs3d_device_supports_inline_rt(struct dxrs3d_device* dev);
int   dxrs3d_device_get_dxr_tier(struct dxrs3d_device* dev);
```

### 1.4 初始化流程

```
dxrs3d_device_create(adapter_index)
  ├→ CreateDXGIFactory2() → EnumAdapters1() 选择 adapter
  ├→ D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, &device)
  ├→ device->QueryInterface(IID_ID3D12Device5, &device5)  /* DXR 要求 */
  ├→ 检查 DXR 支持:
  │    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5)
  │    ├→ RaytracingTier >= D3D12_RAYTRACING_TIER_1_1      → dxr_tier = 2
  │    ├→ RaytracingTier == D3D12_RAYTRACING_TIER_1_0      → dxr_tier = 1
  │    └→ RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED → 报错
  ├→ 创建 Compute CommandQueue (D3D12_COMMAND_LIST_TYPE_COMPUTE)
  ├→ 创建 Copy CommandQueue (D3D12_COMMAND_LIST_TYPE_COPY)
  ├→ 创建 Fence + Event
  ├→ 创建全局 CBV/SRV/UAV Descriptor Heap (容量 1024)
  └→ 创建 Shader Manager (编译/缓存 PSO)
```

### 1.5 命令执行辅助

```cpp
/* 录制并提交计算命令的通用模式 */
struct dxrs3d_cmd_context {
    ID3D12CommandAllocator*     allocator;
    ID3D12GraphicsCommandList4* cmdlist;
    ID3D12CommandQueue*         queue;
    UINT64                      fence_val;
};

res_T dxrs3d_cmd_begin(struct dxrs3d_device* dev, struct dxrs3d_cmd_context* ctx);
res_T dxrs3d_cmd_submit_and_wait(struct dxrs3d_device* dev, struct dxrs3d_cmd_context* ctx);
res_T dxrs3d_cmd_submit_async(struct dxrs3d_device* dev, struct dxrs3d_cmd_context* ctx);
res_T dxrs3d_cmd_wait(struct dxrs3d_device* dev, UINT64 fence_val);
```

---

## 2. dxrs3d_mem — GPU 资源管理

### 2.1 对标: cus3d_mem

custar-3d 有 5 种类型化缓冲。DXR 版本统一为一种泛型缓冲：

```
cus3d_mem:  gpu_buffer_float3, gpu_buffer_float2, gpu_buffer_uint3,
            gpu_buffer_uint32, gpu_buffer_box3f
            → 各 5 个操作 = 25 个函数

dxrs3d_mem: dxrs3d_buffer (统一类型) → 按字节大小管理
            + 类型化上传/下载辅助函数
            → ~10 个核心函数 + 类型化宏/模板
```

### 2.2 数据结构

```cpp
/* 缓冲用途枚举 */
enum dxrs3d_buffer_usage {
    DXRS3D_BUFFER_DEFAULT,       /* GPU-only, SRV/UAV 绑定 */
    DXRS3D_BUFFER_UPLOAD,        /* CPU→GPU 上传暂存 */
    DXRS3D_BUFFER_READBACK,      /* GPU→CPU 回读 */
    DXRS3D_BUFFER_ACCEL_STRUCT,  /* 加速结构专用 (UAV + AS) */
};

struct dxrs3d_buffer {
    ID3D12Resource*         resource;       /* D3D12 资源 */
    D3D12_GPU_VIRTUAL_ADDRESS  gpu_va;      /* GPU 虚拟地址 */
    void*                   mapped_ptr;     /* 持久映射指针 (仅 UPLOAD/READBACK) */
    size_t                  size_bytes;     /* 当前大小 (字节) */
    size_t                  capacity_bytes; /* 已分配容量 */
    enum dxrs3d_buffer_usage usage;         /* 缓冲用途 */

    /* SRV/UAV 描述符索引 (在全局 descriptor heap 中) */
    int                     srv_index;      /* -1 = 无 SRV */
    int                     uav_index;      /* -1 = 无 UAV */
};
```

### 2.3 API 签名

```c
/* 核心缓冲操作 */
res_T dxrs3d_buffer_create(struct dxrs3d_device* dev, struct dxrs3d_buffer* buf,
                           size_t size_bytes, enum dxrs3d_buffer_usage usage);
void  dxrs3d_buffer_destroy(struct dxrs3d_device* dev, struct dxrs3d_buffer* buf);
res_T dxrs3d_buffer_resize(struct dxrs3d_device* dev, struct dxrs3d_buffer* buf,
                           size_t new_size_bytes);

/* 数据传输 (通过 copy queue) */
res_T dxrs3d_buffer_upload(struct dxrs3d_device* dev, struct dxrs3d_buffer* dst,
                           const void* h_src, size_t size_bytes);
res_T dxrs3d_buffer_download(struct dxrs3d_device* dev, void* h_dst,
                             const struct dxrs3d_buffer* src, size_t size_bytes);

/* SRV/UAV 描述符创建 */
res_T dxrs3d_buffer_create_srv(struct dxrs3d_device* dev, struct dxrs3d_buffer* buf,
                               unsigned stride, unsigned num_elements);
res_T dxrs3d_buffer_create_uav(struct dxrs3d_device* dev, struct dxrs3d_buffer* buf,
                               unsigned stride, unsigned num_elements);

/* 便捷宏 — 类型化上传/下载 */
#define dxrs3d_buffer_upload_typed(dev, buf, h_src, count, type) \
    dxrs3d_buffer_upload((dev), (buf), (h_src), (count) * sizeof(type))
#define dxrs3d_buffer_download_typed(dev, h_dst, buf, count, type) \
    dxrs3d_buffer_download((dev), (h_dst), (buf), (count) * sizeof(type))
```

### 2.4 Upload/Download 流程

```
Upload (H2D):
  1. 创建/复用 UPLOAD heap 暂存缓冲
  2. Map → memcpy → Unmap
  3. copy_cmdlist->CopyBufferRegion(dst_default, src_upload)
  4. 提交 copy queue + fence wait

Download (D2H):
  1. 创建/复用 READBACK heap 暂存缓冲
  2. compute_cmdlist->ResourceBarrier(dst → COPY_SOURCE)
  3. copy_cmdlist->CopyBufferRegion(dst_readback, src_default)
  4. 提交 copy queue + fence wait
  5. Map → memcpy → Unmap
```

---

## 3. dxrs3d_types — 公共类型定义

### 3.1 对标: cus3d_types

```cpp
/* ===== GPU 端类型 (HLSL 兼容) ===== */

/* 替代 CUDA float3/float2/uint3 — 使用 DirectXMath 或自定义对齐结构体 */
struct dxrs3d_float3 { float x, y, z; };
struct dxrs3d_float2 { float x, y; };
struct dxrs3d_uint3  { unsigned x, y, z; };

/* 替代 cus3d box3f */
struct dxrs3d_box3f {
    struct dxrs3d_float3 lower;
    struct dxrs3d_float3 upper;
};

/* 图元类型 (不变) */
enum dxrs3d_prim_type {
    DXRS3D_PRIM_TRIANGLE = 0,
    DXRS3D_PRIM_SPHERE   = 1,
};

/* 构建质量 (映射到 DXR Build Flags) */
enum dxrs3d_build_quality {
    DXRS3D_BUILD_LOW    = 0,   /* PREFER_FAST_BUILD */
    DXRS3D_BUILD_MEDIUM = 1,   /* 默认 (平衡) */
    DXRS3D_BUILD_HIGH   = 2,   /* PREFER_FAST_TRACE */
};

/* GPU 球体数据 */
struct dxrs3d_sphere_gpu {
    struct dxrs3d_float3 center;
    float                radius;
};

/* GPU 几何条目 — 对标 geom_gpu_entry */
struct dxrs3d_geom_gpu_entry {
    unsigned geometry_id;
    unsigned instance_id;
    unsigned vertex_offset;   /* 全局顶点偏移 */
    unsigned index_offset;    /* 全局索引偏移 */
    unsigned tri_count;
    unsigned sphere_offset;
    unsigned sphere_count;
    int      prim_type;       /* DXRS3D_PRIM_TRIANGLE / DXRS3D_PRIM_SPHERE */
};

/* GPU 命中结果 — 对标 cus3d_hit_result */
struct dxrs3d_hit_result {
    float    distance;
    float    u, v;            /* 重心坐标或球面参数 */
    float    normal[3];
    unsigned prim_id;         /* BLAS 内的图元 ID */
    unsigned instance_id;     /* TLAS 实例 ID (S3D_INVALID_ID if none) */
    int      hit_type;        /* 0=miss, 1=triangle, 2=sphere */
};

/* GPU Top-K 多命中结果 — 对标 cus3d_multi_hit_result */
#define DXRS3D_MAX_MULTI_HITS 2
struct dxrs3d_multi_hit_result {
    struct dxrs3d_hit_result hits[DXRS3D_MAX_MULTI_HITS];
    int                     count;   /* 实际命中数 [0, DXRS3D_MAX_MULTI_HITS] */
};

/* 实例 GPU 数据 — 对标 instance_gpu_data */
struct dxrs3d_instance_gpu_data {
    float forward_transform[12];     /* 3x4 行主序 */
    float inverse_transform[12];     /* 3x4 行主序 */
    unsigned geometry_id;
    unsigned instance_index;
};
```

---

## 4. dxrs3d_geom_store — 几何数据存储

### 4.1 对标: cus3d_geom_store

职责一致: 将场景中的所有几何体（三角形网格 + 球体）平坦化到连续的 GPU 缓冲中。

**关键差异**: DXR 的 BLAS 构建直接接受顶点/索引缓冲的 GPU 虚拟地址，不需要额外的 AABB 计算步骤（三角形部分由 DXR 自动计算 AABB）。球体需要手动计算 AABB。

### 4.2 数据结构

```cpp
/* 主机端几何条目 — 对标 geom_entry */
struct dxrs3d_geom_entry {
    /* 形状引用 */
    struct s3d_shape* shape;
    struct geometry*  geom;          /* 内部几何对象指针 */

    /* 几何属性 */
    int               prim_type;     /* TRIANGLE / SPHERE */
    unsigned          vertex_offset; /* 全局顶点数组中的起始偏移 */
    unsigned          index_offset;
    unsigned          tri_count;
    unsigned          sphere_offset;
    unsigned          sphere_count;

    /* 过滤器 */
    s3d_hit_filter_function filter_func;
    void*                   filter_data;

    /* 实例化 */
    unsigned          instance_id;
    float             transform[12];
    float             inv_transform[12];
};

struct dxrs3d_geom_store {
    /* 主机端元数据 */
    struct dxrs3d_geom_entry* entries;
    size_t                    entry_count;
    size_t                    entry_capacity;

    /* 全局图元计数 */
    size_t total_tris;
    size_t total_spheres;
    size_t total_vertices;

    /* GPU 缓冲 — 平坦化数组 */
    struct dxrs3d_buffer d_vertices;    /* float3[], 全部顶点 */
    struct dxrs3d_buffer d_indices;     /* uint3[],  全部三角形索引 */
    struct dxrs3d_buffer d_spheres;     /* dxrs3d_sphere_gpu[] */
    struct dxrs3d_buffer d_sphere_aabbs;/* dxrs3d_box3f[], 球体 AABB (DXR AABB geom 输入) */
    struct dxrs3d_buffer d_prim_to_geom;/* uint32[], primID → geom_entry 索引映射 */
    struct dxrs3d_buffer d_geom_entries;/* dxrs3d_geom_gpu_entry[], GPU端元数据 */

    int needs_rebuild;
};
```

### 4.3 API 签名

```c
/* 生命周期 */
res_T dxrs3d_geom_store_create(struct dxrs3d_geom_store** store);
void  dxrs3d_geom_store_destroy(struct dxrs3d_geom_store* store, struct dxrs3d_device* dev);

/* 同步: 遍历场景形状 → 平坦化 GPU 数组 */
res_T dxrs3d_geom_store_sync(struct dxrs3d_geom_store* store, struct s3d_scene* scene,
                             struct dxrs3d_device* dev);

/* 球体 AABB 计算 (Compute Shader 或 CPU) — 三角形 AABB 由 DXR 自动计算 */
res_T dxrs3d_geom_store_compute_sphere_bounds(struct dxrs3d_geom_store* store,
                                              struct dxrs3d_device* dev);

/* 查询 */
const struct dxrs3d_geom_entry* dxrs3d_geom_store_lookup(const struct dxrs3d_geom_store* store,
                                                          unsigned prim_id);
const struct dxrs3d_geom_entry* dxrs3d_geom_store_get_entry(const struct dxrs3d_geom_store* store,
                                                             unsigned geom_idx);
```

### 4.4 与 cus3d_geom_store 的差异

| 方面 | cus3d_geom_store | dxrs3d_geom_store |
|------|-----------------|-------------------|
| AABB 计算 | `compute_bounds()` GPU kernel 计算全部 AABB | 仅需计算球体 AABB，三角形由 DXR 自动处理 |
| 缓冲类型 | 5 种类型化 `gpu_buffer_*` | 统一 `dxrs3d_buffer` |
| 新增缓冲 | — | `d_sphere_aabbs` (DXR AABB geometry 输入) |
| SRV/UAV | 不需要 (CUDA 指针直传) | 需要创建 SRV 绑定到 Compute Shader |

---

## 5. dxrs3d_accel — DXR 加速结构管理

### 5.1 对标: cus3d_bvh

这是 custar-3d → dxrstar-3d 变化最大的模块。DXR 原生提供 BLAS/TLAS 两级加速结构，**无需手动管理 BVH 节点**。

### 5.2 数据结构

```cpp
/* DXR 构建质量映射 */
static const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
    s_build_flags[] = {
    [DXRS3D_BUILD_LOW]    = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD,
    [DXRS3D_BUILD_MEDIUM] = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
    [DXRS3D_BUILD_HIGH]   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
};

/* 每个几何体的 BLAS */
struct dxrs3d_blas_entry {
    struct dxrs3d_buffer as_buffer;    /* BLAS 资源 */
    struct dxrs3d_buffer scratch;      /* 构建暂存 (可释放) */
    int                  valid;
    unsigned             geom_index;   /* 对应 geom_store 条目索引 */
};

/* 实例描述符 (DXR 原生) */
struct dxrs3d_instance_entry {
    D3D12_RAYTRACING_INSTANCE_DESC desc;     /* DXR 实例描述符 */
    unsigned                       blas_idx; /* 对应 BLAS 条目索引 */
    struct geometry*               geom;     /* s3d 几何指针 */
    struct dxrs3d_geom_store*      child_store; /* 子场景的 geom_store (实例) */
    int                            is_pseudo;   /* 伪实例标记 */
};

/* 主加速结构管理器 */
struct dxrs3d_accel {
    /* BLAS 数组 */
    struct dxrs3d_blas_entry* blas_entries;
    size_t                    blas_count;
    size_t                    blas_capacity;

    /* TLAS */
    struct dxrs3d_buffer      tlas_buffer;     /* TLAS 资源 */
    struct dxrs3d_buffer      tlas_scratch;    /* TLAS 构建暂存 */
    struct dxrs3d_buffer      instance_descs;  /* D3D12_RAYTRACING_INSTANCE_DESC[] */

    /* 实例管理 */
    struct dxrs3d_instance_entry* instances;
    size_t                        instance_count;
    size_t                        instance_capacity;

    /* 缓存的场景 AABB */
    float lower[3], upper[3];

    int valid;
    int tlas_valid;
};
```

### 5.3 API 签名

```c
/* 生命周期 */
res_T dxrs3d_accel_create(struct dxrs3d_accel** accel);
void  dxrs3d_accel_destroy(struct dxrs3d_accel* accel, struct dxrs3d_device* dev);

/* BLAS 构建 */
res_T dxrs3d_accel_build_blas(struct dxrs3d_accel* accel,
                               const struct dxrs3d_geom_store* store,
                               struct dxrs3d_device* dev,
                               enum dxrs3d_build_quality quality);

/* 实例化BLAS 构建 */
res_T dxrs3d_accel_build_instance_blas(struct dxrs3d_accel* accel, unsigned instance_idx,
                                        const struct dxrs3d_geom_store* child_store,
                                        struct dxrs3d_device* dev);

/* TLAS 构建 */
res_T dxrs3d_accel_build_tlas(struct dxrs3d_accel* accel, struct dxrs3d_device* dev);

/* 实例变换 */
res_T dxrs3d_accel_set_instance_transform(struct dxrs3d_accel* accel, unsigned instance_idx,
                                           const float forward[12], const float inverse[12]);

/* 查询 */
void  dxrs3d_accel_get_bounds(const struct dxrs3d_accel* accel, float lower[3], float upper[3]);
int   dxrs3d_accel_is_valid(const struct dxrs3d_accel* accel);

/* 实例几何关联 */
res_T dxrs3d_accel_set_instance_geometry(struct dxrs3d_accel* accel, unsigned idx, struct geometry* geom);
res_T dxrs3d_accel_set_instance_child_store(struct dxrs3d_accel* accel, unsigned idx,
                                             struct dxrs3d_geom_store* child);
struct geometry* dxrs3d_accel_get_instance_geometry(const struct dxrs3d_accel* accel, unsigned tlas_idx);

/* 伪实例 */
res_T dxrs3d_accel_register_pseudo_instance(struct dxrs3d_accel* accel, unsigned idx,
                                             struct dxrs3d_geom_store* parent_store);
res_T dxrs3d_accel_reset_instances(struct dxrs3d_accel* accel, struct dxrs3d_device* dev);

/* AS 压缩 (可选优化) */
res_T dxrs3d_accel_compact_blas(struct dxrs3d_accel* accel, unsigned blas_idx,
                                struct dxrs3d_device* dev);
```

### 5.4 BLAS 构建流程

```
dxrs3d_accel_build_blas(accel, store, dev, quality)
  │
  ├→ 遍历 store->entries:
  │    对每个几何体组:
  │
  │    if (prim_type == TRIANGLE):
  │      D3D12_RAYTRACING_GEOMETRY_DESC {
  │        .Type = TRIANGLES,
  │        .Triangles.VertexBuffer = store->d_vertices.gpu_va + vertex_offset * 12,
  │        .Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
  │        .Triangles.VertexCount = vertex_count,
  │        .Triangles.IndexBuffer = store->d_indices.gpu_va + index_offset * 12,
  │        .Triangles.IndexFormat = DXGI_FORMAT_R32_UINT,
  │        .Triangles.IndexCount = tri_count * 3,
  │      }
  │
  │    if (prim_type == SPHERE):
  │      D3D12_RAYTRACING_GEOMETRY_DESC {
  │        .Type = PROCEDURAL_PRIMITIVE_AABBS,
  │        .AABBs.AABBs = store->d_sphere_aabbs.gpu_va + sphere_offset * sizeof(AABB),
  │        .AABBs.AABBCount = sphere_count,
  │      }
  │
  ├→ GetRaytracingAccelerationStructurePrebuildInfo() → 获取所需内存大小
  ├→ 分配 AS buffer + scratch buffer
  ├→ BuildRaytracingAccelerationStructure()
  │    cmdlist->BuildRaytracingAccelerationStructure(&build_desc)
  ├→ UAV barrier (确保 AS 可用)
  └→ 提交并等待
```

### 5.5 TLAS 构建流程

```
dxrs3d_accel_build_tlas(accel, dev)
  │
  ├→ 组装 D3D12_RAYTRACING_INSTANCE_DESC[]:
  │    for (i = 0..instance_count-1):
  │      desc[i].Transform = inst->forward_transform (3x4)
  │      desc[i].InstanceID = i
  │      desc[i].InstanceMask = 0xFF
  │      desc[i].AccelerationStructure = blas_entries[inst->blas_idx].as_buffer.gpu_va
  │
  ├→ 上传 instance_descs 到 GPU
  ├→ GetRaytracingAccelerationStructurePrebuildInfo()
  ├→ 分配 TLAS buffer + scratch
  ├→ BuildRaytracingAccelerationStructure(TOP_LEVEL)
  ├→ UAV barrier
  └→ 提交并等待
```

### 5.6 与 cus3d_bvh 的关键差异

| 方面 | cus3d_bvh | dxrs3d_accel |
|------|----------|-------------|
| BVH 数据 | `cuBQL::BinaryBVH` (节点+叶子数组) | 不透明 AS buffer (`ID3D12Resource`) |
| 构建 | `cuBQL::gpuBuilder()` | `BuildRaytracingAccelerationStructure()` |
| 遍历 | 手动在 kernel 中调用 cuBQL API | DXR `RayQuery` 自动遍历 |
| 两级遍历 | 手动: tlas_to_orig, enterBlas/leaveBlas lambda | DXR 原生: InstanceDesc 自动 |
| 伪实例 | 手动注册 + 恒等变换 | 同样使用恒等变换 InstanceDesc |
| AS 压缩 | 无 | `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION` |
| AS 更新 | 不支持 (总是重建) | 支持 `PERFORM_UPDATE` (refit) |
| scratch | cuBQL 内部管理 | 开发者显式分配/释放 |

---

## 6. dxrs3d_trace — 射线追踪引擎

### 6.1 对标: cus3d_trace

### 6.2 API 签名 (Host 端)

```c
/* 完全对标 cus3d_trace API */

/* 射线批次管理 */
res_T dxrs3d_ray_batch_create(struct dxrs3d_ray_batch* batch, size_t max_rays);
void  dxrs3d_ray_batch_destroy(struct dxrs3d_ray_batch* batch);

/* 批处理 — 单次最近命中 */
res_T dxrs3d_trace_ray_batch(const struct dxrs3d_accel* accel,
                              const struct dxrs3d_geom_store* store,
                              struct dxrs3d_device* dev,
                              const struct dxrs3d_ray_batch* rays,
                              struct dxrs3d_hit_result* h_results);

/* 批处理 — Top-K 多命中 */
res_T dxrs3d_trace_ray_batch_multi(const struct dxrs3d_accel* accel,
                                    const struct dxrs3d_geom_store* store,
                                    struct dxrs3d_device* dev,
                                    const struct dxrs3d_ray_batch* rays,
                                    int max_hits,
                                    struct dxrs3d_multi_hit_result* h_results);

/* 单射线 — 单次最近命中 */
res_T dxrs3d_trace_ray_single(const struct dxrs3d_accel* accel,
                               const struct dxrs3d_geom_store* store,
                               struct dxrs3d_device* dev,
                               const float origin[3], const float direction[3],
                               const float range[2],
                               struct dxrs3d_hit_result* result);

/* 单射线 — Top-K 多命中 */
res_T dxrs3d_trace_ray_single_multi(const struct dxrs3d_accel* accel,
                                     const struct dxrs3d_geom_store* store,
                                     struct dxrs3d_device* dev,
                                     const float origin[3], const float direction[3],
                                     const float range[2], int max_hits,
                                     struct dxrs3d_multi_hit_result* result);
```

### 6.3 射线追踪 HLSL Compute Shader

```hlsl
// shaders/trace_rays_topk.hlsl
// Inline RT Top-K 多命中版本

#define MAX_MULTI_HITS 2

struct HitResult {
    float  distance;
    float  u, v;
    float3 normal;
    uint   primID;
    uint   instanceID;
    int    hitType;    // 0=miss, 1=triangle, 2=sphere
};

struct MultiHitResult {
    HitResult hits[MAX_MULTI_HITS];
    int       count;
};

/* 根常量 */
cbuffer RootConstants : register(b0) {
    uint g_num_rays;
    uint g_max_hits;
};

/* 资源绑定 */
RaytracingAccelerationStructure g_tlas   : register(t0);
StructuredBuffer<float3>        g_origins     : register(t1);
StructuredBuffer<float3>        g_directions  : register(t2);
StructuredBuffer<float2>        g_ranges      : register(t3);
StructuredBuffer<GeomGPUEntry>  g_geom_entries: register(t4);
StructuredBuffer<SphereGPU>     g_spheres     : register(t5);

RWStructuredBuffer<MultiHitResult> g_results  : register(u0);

/* 球体交叉测试 (自定义几何) */
bool IntersectSphere(float3 origin, float3 dir, SphereGPU sphere,
                     out float t, out float3 normal, out float2 uv)
{
    float3 oc = origin - sphere.center;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return false;

    float sqrtD = sqrt(discriminant);
    t = -b - sqrtD;
    if (t < 0.0) t = -b + sqrtD;
    if (t < 0.0) return false;

    float3 hitPoint = origin + t * dir;
    normal = normalize(hitPoint - sphere.center);
    // 球面 UV
    uv.x = atan2(normal.z, normal.x) / (2.0 * 3.14159265) + 0.5;
    uv.y = asin(clamp(normal.y, -1.0, 1.0)) / 3.14159265 + 0.5;
    return true;
}

/* 插入排序维护 Top-K */
void InsertHit(inout MultiHitResult result, HitResult candidate, uint maxK)
{
    if (result.count < (int)maxK) {
        result.hits[result.count++] = candidate;
        // 冒泡排序到正确位置
        for (int j = result.count - 1; j > 0; --j) {
            if (result.hits[j].distance < result.hits[j-1].distance) {
                HitResult tmp = result.hits[j];
                result.hits[j] = result.hits[j-1];
                result.hits[j-1] = tmp;
            }
        }
    } else if (candidate.distance < result.hits[maxK-1].distance) {
        result.hits[maxK-1] = candidate;
        for (int j = (int)maxK - 1; j > 0; --j) {
            if (result.hits[j].distance < result.hits[j-1].distance) {
                HitResult tmp = result.hits[j];
                result.hits[j] = result.hits[j-1];
                result.hits[j-1] = tmp;
            }
        }
    }
}

[numthreads(256, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= g_num_rays) return;

    float3 origin = g_origins[idx];
    float3 direction = g_directions[idx];
    float2 range = g_ranges[idx];

    MultiHitResult result;
    result.count = 0;

    /* ==== Inline Ray Tracing with Top-K collection ==== */
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = range.x;
    ray.TMax = range.y;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, ray);

    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            /* 三角形命中 */
            HitResult hit;
            hit.distance = q.CandidateTriangleRayT();
            hit.u = q.CandidateTriangleBarycentrics().x;
            hit.v = q.CandidateTriangleBarycentrics().y;
            hit.normal = q.CandidateObjectRayDirection(); /* 使用几何法线 */
            hit.primID = q.CandidatePrimitiveIndex();
            hit.instanceID = q.CandidateInstanceID();
            hit.hitType = 1;

            InsertHit(result, hit, g_max_hits);

            /* 如果已收集 K 个，更新 ray.TMax 加速裁剪 */
            if (result.count >= (int)g_max_hits) {
                q.CommitNonOpaqueTriangleHit();
                /* 注意: Inline RT中tMax由CommitNonOpaqueTriangleHit缩紧,
                   或通过 Abort() + 重新 TraceRayInline() */
            }
        }
        else if (q.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE) {
            /* AABB (球体) 命中 — 自定义交叉 */
            uint sphereIdx = q.CandidatePrimitiveIndex();
            SphereGPU sphere = g_spheres[sphereIdx];

            float3 localOrigin = q.CandidateObjectRayOrigin();
            float3 localDir = q.CandidateObjectRayDirection();

            float t; float3 normal; float2 uv;
            if (IntersectSphere(localOrigin, localDir, sphere, t, normal, uv)) {
                if (t >= ray.TMin && t <= ray.TMax) {
                    HitResult hit;
                    hit.distance = t;
                    hit.u = uv.x;
                    hit.v = uv.y;
                    hit.normal = normal;
                    hit.primID = sphereIdx;
                    hit.instanceID = q.CandidateInstanceID();
                    hit.hitType = 2;

                    InsertHit(result, hit, g_max_hits);
                    q.CommitProceduralPrimitiveHit(t);
                }
            }
        }
    }

    /* 若有最终确认的命中，也检查 committed 状态 */
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        /* 已由 commit 自动处理 */
    }

    g_results[idx] = result;
}
```

### 6.4 Host 端批处理流程

```
dxrs3d_trace_ray_batch_multi(accel, store, dev, rays, max_hits, h_results)
  │
  ├→ 1. 创建/复用 GPU 缓冲
  │     d_origins      ← dxrs3d_buffer_upload(rays->h_origins, N)
  │     d_directions   ← dxrs3d_buffer_upload(rays->h_directions, N)
  │     d_ranges       ← dxrs3d_buffer_upload(rays->h_ranges, N)
  │     d_results      ← dxrs3d_buffer_create(N * sizeof(multi_hit_result), UAV)
  │
  ├→ 2. 绑定资源
  │     cmdlist->SetComputeRootSignature(trace_root_sig)
  │     cmdlist->SetPipelineState(trace_topk_pso)
  │     cmdlist->SetComputeRoot32BitConstants(0, {N, max_hits})
  │     cmdlist->SetComputeRootShaderResourceView(1, accel->tlas_buffer.gpu_va)
  │     cmdlist->SetComputeRootDescriptorTable(2, srv_table_start)
  │     cmdlist->SetComputeRootUnorderedAccessView(3, d_results.gpu_va)
  │
  ├→ 3. Dispatch
  │     uint groups = (N + 255) / 256;
  │     cmdlist->Dispatch(groups, 1, 1);
  │
  ├→ 4. UAV Barrier → 确保写入完成
  ├→ 5. 提交并等待
  ├→ 6. 下载结果
  │     dxrs3d_buffer_download(h_results, d_results, N * sizeof(multi_hit_result))
  └→ 7. 清理临时缓冲 (或复用)
```

### 6.5 Top-K 在 Inline RT 中的实现细节

DXR Inline RT 的 `RayQuery` 提供了对遍历过程的完全控制：

```
RayQuery 状态机:
  ┌─────────────────────────────────────────────────────┐
  │ TraceRayInline(tlas, flags, mask, ray)               │
  └──────────────────────┬──────────────────────────────┘
                         ▼
  ┌─────────── Proceed() ──────────────┐
  │  返回 true: 有候选命中              │
  │  ├→ CandidateType()                │
  │  │   ├→ NON_OPAQUE_TRIANGLE        │
  │  │   │   可调用 CommitNonOpaque()   │
  │  │   │   或跳过 (相当于 AnyHit 拒绝)│
  │  │   └→ PROCEDURAL_PRIMITIVE       │
  │  │       自定义交叉测试              │
  │  │       CommitProceduralHit(t)     │
  │  └→ 继续 Proceed()                 │
  │                                     │
  │  返回 false: 遍历完成               │
  │  ├→ CommittedStatus()              │
  │  │   ├→ COMMITTED_TRIANGLE_HIT     │
  │  │   ├→ COMMITTED_PROCEDURAL_HIT   │
  │  │   └→ COMMITTED_NOTHING          │
  │  └→ 读取 committed 命中属性        │
  └─────────────────────────────────────┘
```

**Top-K 实现策略**:
- 将所有几何体标记为 **non-opaque**（`D3D12_RAYTRACING_GEOMETRY_FLAG_NONE`）
- 这样每个候选命中都会触发 `Proceed()` 返回，而不是自动 commit
- 开发者在每次 `Proceed()` 时决定是否 commit，实现自定义的 Top-K 收集
- 当收集满 K 个命中后，不再 commit，遍历自然结束（tMax 不缩小）

**替代策略** (更精确):
- 使用多次 `TraceRayInline()` 调用，每次缩小 range
- 第 1 次找到最近命中 → 记录 t1
- 第 2 次用 range = [t1+epsilon, tMax] → 找到第 2 近命中
- 简单但多次遍历会有性能开销

---

## 7. dxrs3d_closest_point — 最近点查询

### 7.1 挑战

DXR **不直接提供最近点查询** (`rtcPointQuery` 在 Embree 中有，cuBQL 有 `shrinkingRadiusQuery`)。需要自行实现。

### 7.2 实现策略

**方案 A: AABB 搜索 + Compute Shader** (推荐)

```
1. 从查询点出发，构建初始搜索球体 (半径 = max_radius)
2. 使用 Inline RT 射线追踪多方向采样搜索候选三角形
3. 对候选三角形计算精确点-三角形距离
4. 迭代缩小搜索半径直到收敛

替代: 使用单独的 BVH (CPU 构建, GPU 存储) 做空间搜索
```

**方案 B: 多射线近似法** (与 custar-3d 包壳定位类似)

```
1. 从查询点向多个方向发射射线 (如 26 方向 = 立方体面/边/角)
2. 找到所有命中的三角形
3. 对命中三角形计算精确最近点
4. 返回距离最小的
```

**方案 C: GPU BVH 手动遍历** (最精确但复杂)

自行在 GPU 上构建/遍历一个辅助 BVH，实现 shrinking radius query。这本质上重新实现了 cuBQL 的功能。

### 7.3 推荐方案: B (多射线近似法)

考虑到:
- 最近点查询在 stardis 中的使用频率远低于射线追踪
- 方案 B 可以复用已有的 Inline RT 基础设施
- 精度对于热传输模拟通常足够
- 实现复杂度最低

```c
/* API 保持与 cus3d 完全一致 */
res_T dxrs3d_cp_batch_create(struct dxrs3d_cp_batch* batch, size_t max_queries);
void  dxrs3d_cp_batch_destroy(struct dxrs3d_cp_batch* batch);
res_T dxrs3d_closest_point_batch(const struct dxrs3d_accel* accel,
                                  const struct dxrs3d_geom_store* store,
                                  struct dxrs3d_device* dev,
                                  const struct dxrs3d_cp_batch* queries,
                                  struct dxrs3d_cp_result* h_results);
```

### 7.4 最近点 HLSL Compute Shader 方案概况

```hlsl
// shaders/closest_point.hlsl
// 多射线近似法: 从查询点向多方向发射射线，找最近命中

static const float3 SEARCH_DIRS[26] = { /* 立方体 6 面 + 12 棱 + 8 角 */ };

[numthreads(256, 1, 1)]
void ClosestPointCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= g_num_queries) return;

    float3 pos = g_positions[idx];
    float  radius = g_radii[idx];
    float  bestDist = radius;
    CPResult bestResult = (CPResult)0;
    bestResult.distance = radius;

    for (uint d = 0; d < 26; ++d) {
        RayDesc ray;
        ray.Origin = pos;
        ray.Direction = SEARCH_DIRS[d];
        ray.TMin = 0.0;
        ray.TMax = radius;

        RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
        q.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) { /* accept all */ q.CommitNonOpaqueTriangleHit(); }

        if (q.CommittedStatus() != COMMITTED_NOTHING) {
            float t = q.CommittedRayT();
            uint primID = q.CommittedPrimitiveIndex();
            // 计算精确最近点距离 (点到三角形)
            float dist = ComputePointTriangleDist(pos, primID);
            if (dist < bestDist) {
                bestDist = dist;
                bestResult = BuildCPResult(primID, dist, ...);
            }
        }
    }

    g_results[idx] = bestResult;
}
```

---

## 8. dxrs3d_find_enclosure — 包壳定位

### 8.1 对标: cus3d_find_enclosure

使用与 custar-3d 完全相同的 6-ray 旋转方法，但使用 DXR Inline RT 替代 cuBQL 射线遍历。

### 8.2 HLSL 实现概况

```hlsl
// shaders/find_enclosure.hlsl
// 6-ray 包壳定位: 从查询点向 ±X, ±Y, ±Z 发射射线，
// 通过命中三角形的正面/背面判断内外关系

static const float3 AXIS_DIRS[6] = {
    float3(1,0,0), float3(-1,0,0),
    float3(0,1,0), float3(0,-1,0),
    float3(0,0,1), float3(0,0,-1)
};

[numthreads(256, 1, 1)]
void FindEnclosureCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= g_num_queries) return;

    float3 pos = g_positions[idx];
    EncResult result = (EncResult)0;
    result.enc_id = -1;

    for (uint d = 0; d < 6; ++d) {
        RayDesc ray;
        ray.Origin = pos;
        ray.Direction = AXIS_DIRS[d];
        ray.TMin = 1e-6;
        ray.TMax = 1e30;

        RayQuery<RAY_FLAG_NONE> q;
        q.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) { q.CommitNonOpaqueTriangleHit(); }

        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            float3 hitNormal = q.CommittedTriangleFrontFace()
                ? q.CommittedObjectRayDirection()   /* 更新法线 */
                : -q.CommittedObjectRayDirection();
            int side = q.CommittedTriangleFrontFace() ? 0 : 1;
            // 记录命中信息用于包壳判定 ...
        }
    }

    // 综合 6 射线结果判定包壳 ID
    g_results[idx] = result;
}
```

---

## 9. dxrs3d_shader_mgr — PSO 管理 (新增)

### 9.1 职责

管理 HLSL Compute Shader 的编译、Pipeline State Object (PSO) 创建和缓存。这是 DXR 相对于 CUDA 新增的基础设施需求。

### 9.2 数据结构

```cpp
/* 预定义 shader ID */
enum dxrs3d_shader_id {
    DXRS3D_SHADER_TRACE_RAYS,           /* 单次最近命中 */
    DXRS3D_SHADER_TRACE_RAYS_TOPK,      /* Top-K 多命中 */
    DXRS3D_SHADER_CLOSEST_POINT,        /* 最近点查询 */
    DXRS3D_SHADER_FIND_ENCLOSURE,       /* 包壳定位 */
    DXRS3D_SHADER_COMPUTE_BOUNDS,       /* AABB 计算 */
    DXRS3D_SHADER_COUNT
};

struct dxrs3d_pso_entry {
    ID3D12PipelineState*    pso;
    ID3D12RootSignature*    root_sig;
    int                     valid;
};

struct dxrs3d_shader_mgr {
    struct dxrs3d_pso_entry  psos[DXRS3D_SHADER_COUNT];
    IDxcCompiler3*           compiler;      /* DXC 编译器 */
    IDxcUtils*               utils;
    ID3D12Device5*           device;        /* 弱引用 */
};
```

### 9.3 API

```c
res_T dxrs3d_shader_mgr_create(struct dxrs3d_shader_mgr** mgr, ID3D12Device5* device);
void  dxrs3d_shader_mgr_destroy(struct dxrs3d_shader_mgr* mgr);
res_T dxrs3d_shader_mgr_get_pso(struct dxrs3d_shader_mgr* mgr, enum dxrs3d_shader_id id,
                                 ID3D12PipelineState** pso, ID3D12RootSignature** root_sig);
```

### 9.4 Root Signature 设计 (统一布局)

```
Root Parameter 0: Root Constants (32-bit × 4)
  ├─ num_rays / num_queries
  ├─ max_hits
  ├─ flags
  └─ reserved

Root Parameter 1: SRV (TLAS)
  └─ t0: RaytracingAccelerationStructure

Root Parameter 2: Descriptor Table (SRV)
  ├─ t1: origins / positions
  ├─ t2: directions / radii
  ├─ t3: ranges
  ├─ t4: geom_entries
  ├─ t5: spheres
  ├─ t6: vertices
  └─ t7: indices

Root Parameter 3: Descriptor Table (UAV)
  └─ u0: results
```

---

## 10. dxrs3d_descriptor — Descriptor Heap 管理 (新增)

### 10.1 职责

管理 CBV/SRV/UAV descriptor heap 的分配和回收。这是 D3D12 特有的基础设施。

### 10.2 数据结构

```cpp
struct dxrs3d_descriptor_heap {
    ID3D12DescriptorHeap* heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start;
    UINT  increment_size;
    UINT  capacity;
    UINT  next_free;          /* 简单线性分配 */
    UINT* free_list;          /* 回收列表 (可选) */
    UINT  free_count;
};
```

### 10.3 API

```c
res_T dxrs3d_descriptor_heap_create(struct dxrs3d_descriptor_heap** heap,
                                     ID3D12Device* device,
                                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                                     UINT capacity, int shader_visible);
void  dxrs3d_descriptor_heap_destroy(struct dxrs3d_descriptor_heap* heap);

/* 分配/释放描述符槽位 */
int   dxrs3d_descriptor_heap_alloc(struct dxrs3d_descriptor_heap* heap);
void  dxrs3d_descriptor_heap_free(struct dxrs3d_descriptor_heap* heap, int index);

/* 获取描述符句柄 */
D3D12_CPU_DESCRIPTOR_HANDLE dxrs3d_descriptor_cpu(struct dxrs3d_descriptor_heap* heap, int index);
D3D12_GPU_DESCRIPTOR_HANDLE dxrs3d_descriptor_gpu(struct dxrs3d_descriptor_heap* heap, int index);
```

---

## 11. dxrs3d_prim / dxrs3d_trace_util — 命中结果转换

### 11.1 对标: cus3d_prim + cus3d_trace_util

这两个模块的逻辑与 custar-3d **基本相同**（纯 CPU 端代码），仅需：
1. 将 `cus3d_hit_result` → `dxrs3d_hit_result` 类型替换
2. 将 `cus3d_geom_store` → `dxrs3d_geom_store` 类型替换
3. 保持 UV/法线修正逻辑不变

### 11.2 DXR 特有的法线约定差异

| 来源 | 约定 | 说明 |
|------|------|------|
| DXR `RayQuery` | 提供 `CommittedTriangleFrontFace()` 判断正面/背面 | 无需手动计算 |
| DXR 重心坐标 | `(bary.x, bary.y)` = 标准重心坐标 `(β, γ)` | α = 1 - β - γ |
| cuBQL/Moller-Trumbore | `(u, v)` | w = 1 - u - v |
| s3d 约定 | `(w=1-u-v, u)` | 从 Moller-Trumbore 转换 |

DXR 的重心坐标约定可能与 cuBQL 的 Moller-Trumbore 约定不同，需要在 `trace_hit_fixup()` 中调整转换公式。

```cpp
/* DXR版: trace_hit_fixup */
static inline void trace_hit_fixup_dxr(struct dxrs3d_hit_result* hit) {
    if (hit->hit_type == 1) { /* triangle */
        /* DXR barycentrics: (β, γ), α = 1 - β - γ
           s3d 约定: uv[0] = α = 1-β-γ, uv[1] = β */
        float beta  = hit->u;
        float gamma = hit->v;
        float alpha = 1.0f - beta - gamma;

        /* clamp */
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        if (beta  < 0.0f) beta  = 0.0f;
        if (beta  > 1.0f) beta  = 1.0f;

        hit->u = alpha;  /* s3d uv[0] */
        hit->v = beta;   /* s3d uv[1] */

        /* DXR 法线: CommittedTriangleFrontFace() 已告知方向,
           可能仍需对齐 s3d 的 CW 约定 — 具体取决于顶点缠绕序 */
    }
}
```

---

## 12. 模块依赖图

```
┌─────────────┐
│ rsys (基础)  │
└──────┬──────┘
       │
┌──────▼──────┐     ┌────────────────┐
│ dxrs3d_     │────→│ dxrs3d_        │
│ device      │     │ descriptor     │
└──────┬──────┘     └────────┬───────┘
       │                     │
┌──────▼──────┐     ┌────────▼───────┐
│ dxrs3d_     │     │ dxrs3d_        │
│ mem         │     │ shader_mgr     │
└──────┬──────┘     └────────┬───────┘
       │                     │
┌──────▼──────┐              │
│ dxrs3d_     │              │
│ geom_store  │              │
└──────┬──────┘              │
       │                     │
┌──────▼──────┐              │
│ dxrs3d_     │◀─────────────┘
│ accel       │
└──────┬──────┘
       │
 ┌─────┼──────────────────┐
 │     │                  │
┌▼─────▼──┐  ┌──────────┐│ ┌────────────────┐
│dxrs3d_  │  │dxrs3d_   ││ │dxrs3d_         │
│trace    │  │closest_  ││ │find_enclosure  │
│         │  │point     ││ │                │
└────┬────┘  └──────────┘│ └────────────────┘
     │                    │
┌────▼────┐               │
│dxrs3d_  │               │
│prim     │◀──────────────┘
└─────────┘
```

---

*下一步: 参阅 [api-mapping.md](api-mapping.md) 获取完整的 API 一一映射表*
