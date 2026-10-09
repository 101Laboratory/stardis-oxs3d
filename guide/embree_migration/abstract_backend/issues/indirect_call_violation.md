# 非法间接调用
## 问题现场
[s3d_backend.c](../../../../stardis-cpu-backend-abstraction/star-3d/0.10/src/s3d_backend.c) line 593 - 607
```c
/* Embree bounds function wrapper */
static void 
embree_bounds_function_wrapper(const struct RTCBoundsFunctionArguments* args)
{
    struct s3d_backend_bounds_function_args backend_args;
    backend_args.geometry_userdata = args->geometryUserPtr;
    backend_args.prim_id = args->primID;
    backend_args.time_step = args->timeStep;
    backend_args.bounds_lower = &args->bounds_o->lower_x;
    backend_args.bounds_upper = &args->bounds_o->upper_x;
    
    s3d_backend_bounds_function func = 
        (s3d_backend_bounds_function)args->geometryUserPtr;
    func(&backend_args);
}
```

line 606处，`args->geometryUserPtr`被当作函数指针调用，但实际上它是一个数据指针，导致非法间接调用错误。

## 根因错误代码

[s3d_backend.c](../../../../stardis-cpu-backend-abstraction/star-3d/0.10/src/s3d_backend.c) s3d_backend_geometry_set_bounds_function(s3d_backend_geometry* geometry, s3d_backend_bounds_function func, void* user_data) line 618
```c
        rtcSetGeometryBoundsFunction(
            geometry->rtc_geometry,
            embree_bounds_function_wrapper,
            (void*)func);  /* 传递函数指针作为userPtr */
```

## 对比原始内容
```c
void
geometry_rtc_sphere_bounds(const struct RTCBoundsFunctionArguments* args)
{
  struct geometry* geom;
  struct sphere sphere;
  ASSERT(args && args->primID == 0 && args->timeStep == 0);

  geom = args->geometryUserPtr;
  ASSERT(geom && geom->type == GEOM_SPHERE);

  sphere = *geom->data.sphere;
  args->bounds_o->lower_x = sphere.pos[0] - sphere.radius;
  args->bounds_o->lower_y = sphere.pos[1] - sphere.radius;
  args->bounds_o->lower_z = sphere.pos[2] - sphere.radius;
  args->bounds_o->upper_x = sphere.pos[0] + sphere.radius;
  args->bounds_o->upper_y = sphere.pos[1] + sphere.radius;
  args->bounds_o->upper_z = sphere.pos[2] + sphere.radius;
}
```
```c
rtcSetGeometryBoundsFunction(geom->rtc, geometry_rtc_sphere_bounds, NULL);
```