//skinning: vertices through one bone's matrix or a blend of two
#include "engine/native/native.h"

namespace ge::native
{

//ge_skin1 (803A9FA0), ge_skin2 (803AA2E4)
//(n, 3x4 matrix, in, out[, -, second matrix, weights]) -> the end of out
//a vertex is 16 bytes: a position (floats), a normal (three elements quantised by GQR7)
//and a fourth element that ends up as the sum the paired halves leave there
//with two bones the position is blended by the pair of weights (GQR6, 4 bytes a vertex),
//the normal only takes the first matrix and the first weight
struct Mtx34 {
    float a[12];
    explicit Mtx34(uint32_t p) { for (int i = 0; i < 12; ++i) a[i] = F(p + 4 * i); }
    float pos(int k, float x, float y, float z) const { return madd(a[6 + k], z, madd(a[3 + k], y, madd(a[k], x, a[9 + k]))); }
    float dir(int k, float x, float y, float z) const { return madd(a[6 + k], z, madd(a[3 + k], y, a[k] * x)); }
};

void skin(PPCContext& c, bool two) {
    uint32_t n = c.r[3], s = c.r[5], d = c.r[6], wp = c.r[9];
    if (!n || !ram(s) || !ram(d)) { g_routines[two ? kSkin2Routine : kSkin1Routine].orig(c); return; }
    const Mtx34 A(c.r[4]);
    const Quant in = gqr_load(c, 7), out = gqr_store(c, 7);
    if (!two) {
        for (; n; --n, s += 16, d += 16) {
            float x = F(s), y = F(s + 4), z = F(s + 8);
            float nx = in.get(s + 12), ny = in.get(s + 12 + in.size), nz = in.get(s + 14);
            float p[3], q[4];
            for (int k = 0; k < 3; ++k) { p[k] = A.pos(k, x, y, z); q[k] = A.dir(k, nx, ny, nz); }
            q[3] = nz + (ny + nx);
            for (int k = 0; k < 3; ++k) S(d + 4 * k, p[k]);
            out.put(d + 12, q[0]); out.put(d + 12 + out.size, q[1]);
            out.put(d + 14, q[2]); out.put(d + 14 + out.size, q[3]);
        }
    } else {
        const Mtx34 B(c.r[8]);
        const Quant wq = gqr_load(c, 6);
        for (; n; --n, s += 16, d += 16, wp += 4) {
            float x = F(s), y = F(s + 4), z = F(s + 8);
            float nx = in.get(s + 12), ny = in.get(s + 12 + in.size), nz = in.get(s + 14);
            float w0 = wq.get(wp), w1 = wq.get(wp + wq.size);
            float p[3], q[4];
            for (int k = 0; k < 3; ++k) {
                p[k] = madd(B.pos(k, x, y, z), w1, A.pos(k, x, y, z) * w0);
                q[k] = A.dir(k, nx, ny, nz) * w0;
            }
            q[3] = (nz + (ny + nx)) * w0;
            for (int k = 0; k < 3; ++k) S(d + 4 * k, p[k]);
            out.put(d + 12, q[0]); out.put(d + 12 + out.size, q[1]);
            out.put(d + 14, q[2]); out.put(d + 14 + out.size, q[3]);
        }
    }
    c.r[3] = d;
}
void skin1(PPCContext& c) { skin(c, false); }
void skin2(PPCContext& c) { skin(c, true); }

} //namespace ge::native
