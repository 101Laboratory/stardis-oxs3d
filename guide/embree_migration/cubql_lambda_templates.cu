
// Example cuBQL procedural primitive lambda mapping Embree User Geometry

struct MySphere {
    float3 center;
    float radius;
    int materialId;
};

struct MyHit {
    float t;
    int primID;
    int geomID;
};

// Ray definition compatible with cuBQL
struct MyRay {
    float3 org;
    float3 dir;
    float tMin;
    float tMax;
    unsigned int mask;
};

// Device-side intersect lambda
auto intersectLambda = [=] __device__ (
    const MyRay& ray,
    int primID,
    MyHit& hit
) -> int {
    const MySphere& sph = spheres[primID];

    // Optional mask test
    if ((ray.mask & sph.mask) == 0)
        return CUBQL_CONTINUE_TRAVERSAL;

    // Ray-sphere intersection
    float3 oc = ray.org - sph.center;
    float b = dot(oc, ray.dir);
    float c = dot(oc, oc) - sph.radius * sph.radius;
    float disc = b*b - c;

    if (disc < 0.0f)
        return CUBQL_CONTINUE_TRAVERSAL;

    float t = -b - sqrtf(disc);
    if (t < ray.tMin || t > ray.tMax)
        return CUBQL_CONTINUE_TRAVERSAL;

    // Closest hit update
    hit.t = t;
    hit.primID = primID;
    hit.geomID = 0;

    // Early exit for occlusion rays
    // return CUBQL_TERMINATE_TRAVERSAL;

    return CUBQL_CONTINUE_TRAVERSAL;
};

// Traversal call (closest hit)
cubql::shrinkingRayQuery(
    bvh,
    ray,
    intersectLambda
);
