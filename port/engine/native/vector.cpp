//vectors: a plane against a ray, a point in a triangle, a point against a segment
#include "engine/native/native.h"

namespace ge::native
{

//ge_plane_ray (803AB2B4): (plane {n, d}, f1 epsilon, out t, origin, direction) -> hit?
void plane_ray(PPCContext& c) {
    uint32_t pl = c.r[3], o = c.r[5], d = c.r[6];
    float nx = F(pl), ny = F(pl + 4), nz = F(pl + 8);
    float den = madd(nz, F(d + 8), madd(ny, F(d + 4), nx * F(d)));
    c.r[3] = 0;
    if (std::fabs((double)den) < c.f[1]) return;
    float num = -madd(nz, F(o + 8), madd(ny, F(o + 4), madd(nx, F(o), F(pl + 12))));
    float r = (float)(1.0 / (double)den); //fres, then one Newton step
    float inv = madd(r, madd(-r, den, F(c.r[2] - 0x2538)), r);
    S(c.r[4], num * inv);
    c.r[3] = 1;
}

//ge_tri_contains (803AB324): (-, a, b, c, point, normal) -> inside all three edges?
bool TriContains(const float v[3][3], const float* p, const float* n)
{
    static const int from[3] = {2, 0, 1}, to[3] = {0, 1, 2}; //the edges c>a, a>b, b>c and the point from a, b, c
    for (int e = 0; e < 3; ++e) {
        const float* a = v[from[e]];
        const float* b = v[to[e]];
        float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
        float cx = madd(n[2], ey, -(n[1] * ez)), cy = madd(n[0], ez, -(n[2] * ex)), cz = madd(n[1], ex, -(n[0] * ey));
        float side = madd(p[2] - b[2], cz, madd(p[1] - b[1], cy, (p[0] - b[0]) * cx));
        if (side > 0.0f) return false;
    }
    return true;
}
void tri_contains(PPCContext& c) {
    float v[3][3], p[3], n[3];
    const uint32_t src[3] = {c.r[4], c.r[5], c.r[6]};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) v[i][k] = F(src[i] + 4 * k);
        p[i] = F(c.r[7] + 4 * i);
        n[i] = F(c.r[8] + 4 * i);
    }
    c.r[3] = TriContains(v, p, n);
}

//ge_vec4_transform (800164B0): v = M v; ge_vec4_sub (80014CE0): out = a - b
void vec4_transform(PPCContext& c) {
    uint32_t v = c.r[3], m = c.r[4];
    float x = F(v), y = F(v + 4), z = F(v + 8), w = F(v + 12), o[4];
    for (int k = 0; k < 4; ++k)
        o[k] = madd(F(m + 0x30 + 4 * k), w, madd(F(m + 0x20 + 4 * k), z, madd(F(m + 0x10 + 4 * k), y, F(m + 4 * k) * x)));
    for (int k = 0; k < 4; ++k) S(v + 4 * k, o[k]);
}
void vec4_sub(PPCContext& c) {
    uint32_t o = c.r[3], a = c.r[4], b = c.r[5];
    float t[4];
    for (int k = 0; k < 4; ++k) t[k] = F(a + 4 * k) - F(b + 4 * k);
    for (int k = 0; k < 4; ++k) S(o + 4 * k, t[k]);
}

//ge_segment_closest (8033C7B0): (segment {origin, direction}, point, out)
//-> 1 the origin, 2 a point between, 3 the end (vectors of four floats; the dot products take three)
int SegmentClosest(const float* o, const float* d, const float* p, float zero, float* out) {
    auto dot3 = [&](float x, float y, float z) { return madd(d[2], z, d[0] * x + d[1] * y); };
    float e[4];
    for (int k = 0; k < 4; ++k) e[k] = o[k] + d[k];
    if (!(dot3(e[0] - p[0], e[1] - p[1], e[2] - p[2]) > zero)) {
        for (int k = 0; k < 4; ++k) out[k] = e[k];
        return 3;
    }
    float t = dot3(p[0] - o[0], p[1] - o[1], p[2] - o[2]), len = 0;
    if (!(t > zero) || !((len = dot3(d[0], d[1], d[2])) > zero)) {
        for (int k = 0; k < 4; ++k) out[k] = o[k];
        return 1;
    }
    t /= len;
    for (int k = 0; k < 4; ++k) out[k] = madd(d[k], t, o[k]);
    return 2;
}
void segment_closest(PPCContext& c) {
    uint32_t sg = c.r[3], pt = c.r[4], out = c.r[5];
    float o[4], d[4], p[4], r[4];
    for (int k = 0; k < 4; ++k) { o[k] = F(sg + 4 * k); d[k] = F(sg + 0x10 + 4 * k); p[k] = F(pt + 4 * k); }
    c.r[3] = SegmentClosest(o, d, p, F(c.r[2] - 0x2E04), r);
    for (int k = 0; k < 4; ++k) S(out + 4 * k, r[k]);
}

} //namespace ge::native
