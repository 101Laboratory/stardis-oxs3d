# Fix: test_s3d_shape SegFault — Instance child_scene 引用泄漏

**测试**: #14 test_s3d_shape  
**症状**: SegFault（无行号），所有 CHK 断言通过后进程退出时崩溃  
**退出码**: SegFault（非 0xc0000409）  
**分类**: 内存安全 / 生命周期  

---

## 一、根因

`s3d_shape_ref_put`（[ox_s3d_shape.cpp L277-L283](../../optix-throughput-validation/s3d_wrapper/ox_s3d_shape.cpp)）在 shape 引用归零时仅调用 `delete shape`，**未释放 instance 类型 shape 持有的 `child_scene` 引用**。

当前实现：
```cpp
res_T s3d_shape_ref_put(s3d_shape* shape) {
    if (!shape) return RES_BAD_ARG;
    if (shape->ref == 0) return RES_BAD_ARG;
    if (--shape->ref == 0) {
        delete shape;  // ← 只做 delete，不释放 child_scene 引用
    }
    return RES_OK;
}
```

CPU 参考实现 [s3d_shape.c shape_release](../../stardis-cpu/star-3d/0.10/src/s3d_shape.c) 在 shape 销毁时根据类型分别清理子资源：
```c
case GEOM_INSTANCE:
    if(shape->data.instance) instance_ref_put(shape->data.instance);
    // instance_release → scene_ref_put(scn)  ← 释放 child_scene 引用
    break;
```

GPU 实现遗漏了这一逻辑。

---

## 二、引用链推演

### 测试清理序列（test_s3d_shape.c 末尾）

对象初始状态（清理前）：

| 对象 | ref | 持有者 |
|------|-----|--------|
| shape | 2 | 调用者(1) + scn.shapes(1) |
| inst | 1 | 调用者(1)，inst.child_scene → scn |
| shape_copy | 1 | 调用者(1) |
| scn | 2 | 调用者(1) + inst.child_scene(1) |
| dev | 2 | 调用者(1) + scn.dev(1) |

### 当前（有 Bug）

| 步骤 | 操作 | 效果 | 残留 |
|------|------|------|------|
| 1 | `ref_put(shape)` | shape.ref 2→1 | shape 仍被 scn 持有 |
| 2 | `ref_put(inst)` | inst.ref 1→0, `delete inst` | **scn.ref 仍=2**（child_scene 引用未释放）|
| 3 | `ref_put(shape_copy)` | copy.ref 1→0, delete | — |
| 4 | `ref_put(scn)` | scn.ref 2→**1** | **scene 不销毁** → shape 不释放 → dev 不释放 |
| 5 | `ref_put(dev)` | dev.ref 2→**1** | **device 不销毁** → `shutdown()` 不调用 |
| 退出 | 进程退出 | CUDA atexit 钩子遇到未释放的 OptiX context | **💥 SegFault** |

### 修复后

| 步骤 | 操作 | 效果 |
|------|------|------|
| 1 | `ref_put(shape)` | shape.ref 2→1 |
| 2 | `ref_put(inst)` | inst.ref 1→0 → **`scene_ref_put(child_scene)`** → scn.ref 2→1 → `delete inst` |
| 3 | `ref_put(shape_copy)` | copy.ref 1→0, delete |
| 4 | `ref_put(scn)` | scn.ref 1→**0** → scene 销毁 → `ref_put(shape)` → shape.ref 1→0 → delete; `ref_put(dev)` → dev.ref 2→1 |
| 5 | `ref_put(dev)` | dev.ref 1→**0** → `shutdown()` → `optixDeviceContextDestroy` → 正常清理 |
| 退出 | 进程退出 | CUDA context 已清理 → ✅ 无 SegFault |

---

## 三、修复方案

### 修改文件

**`optix-throughput-validation/s3d_wrapper/ox_s3d_shape.cpp`** — `s3d_shape_ref_put` 函数

### 修改内容

```diff
 res_T s3d_shape_ref_put(s3d_shape* shape) {
     if (!shape) return RES_BAD_ARG;
     if (shape->ref == 0) return RES_BAD_ARG;
     if (--shape->ref == 0) {
+        /* Release child_scene reference for instance shapes */
+        if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
+            s3d_scene_ref_put(shape->child_scene);
+            shape->child_scene = nullptr;
+        }
         delete shape;
     }
     return RES_OK;
 }
```

### 不需要修改的地方

| 文件 | 原因 |
|------|------|
| `ox_s3d_scene.cpp` — `s3d_scene_ref_put` | ✅ 已正确释放 shapes + device |
| `ox_s3d_device.cpp` — `s3d_device_ref_put` | ✅ 处于依赖链底端，无子资源 |
| `ox_s3d_internal.h` — `s3d_shape` struct | ✅ 无需新增字段 |
| Mesh/Sphere shape 清理 | ✅ `std::vector` 由 `delete` 自动析构 |

### 头文件依赖

`ox_s3d_shape.cpp` 已 `#include "ox_s3d_internal.h"`，`s3d_scene_ref_put` 在 `s3d.h` 中声明为 `extern "C"`，**无需新增 include**。

---

## 四、设计决策

**不为 GPU shape 添加 device 引用管理**：CPU 版本的 shape 持有 `dev` 引用并在 `shape_release` 中调用 `device_ref_put(dev)`。GPU 实现的 `s3d_shape` 结构体无 `dev` 字段，device 引用仅通过 scene 链管理（shape → scene.shapes, scene → device）。此设计可行，无需增加复杂度。

---

## 五、验证

```powershell
cd optix-throughput-validation/build_s3d
cmake --build . --config Release --target test_s3d_shape > build.log 2>&1
.\bin\Release\test_s3d_shape.exe
```

预期：
- SegFault 消失
- 测试正常退出（exit code 0）

回归测试：
```powershell
ctest -C Release --output-on-failure
```

重点关注涉及 instance 生命周期的测试：
- test_s3d_sphere_instance（#17）
- test_s3d_scene（#10）
- test_s3d_trace_ray_instance（#19）

---

## 六、次要发现

| 问题 | 位置 | 严重度 |
|------|------|--------|
| `s3d_mesh_get_triangle_indices` 不检查 `ids == NULL` | ox_s3d_shape.cpp ~L151 | 低 |
| 测试中 `pos[3]`/`trans[12]` 未初始化传给 instance API | test_s3d_shape.c | 信息级（CPU 版同样）|