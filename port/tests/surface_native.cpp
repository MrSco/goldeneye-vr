#include "gevr_surface_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#define MAX_VERTICES 128
#define SUPPORT_CHECK(x) assert(x)
struct LoadedVertex {
    float x, y, z, w, u, v;
    struct { uint8_t r, g, b, a; } color;
    uint8_t fog, clip_rej;
};
static struct {
    LoadedVertex loaded_vertices[MAX_VERTICES];
    struct { uint16_t s, t; } texture_scaling_factor;
} rsp;
static struct { struct { uint8_t a; } fog_color; } rdp;
static float aspect = 1.0f;
static float gfx_adjust_x_for_aspect_ratio(float x, float w) { return (x + w * 0.1f) * aspect; }
/* INSERT_SKY_VERTEX_LOADER */

static double interpolate(const LoadedVertex *v, double a, double b, bool t) {
    double c = 1.0 - a - b;
    return (a * (t ? v[0].v : v[0].u) / v[0].w
        + b * (t ? v[1].v : v[1].u) / v[1].w
        + c * (t ? v[2].v : v[2].u) / v[2].w)
        / (a / v[0].w + b / v[1].w + c / v[2].w);
}

static void water() {
    /* Near/horizon range from the headset trace, not game asset data. */
    GevrSkyVertex input[3];
    const float xy[3][2] = {{319.75f,229.75f},{0,229.75f},{0,150.757f}};
    const float depth[3] = {73.885f,79.8364f,195839.0f};
    const float st[3][2] = {{-929.682f,389.714f},{-1094.65f,433.115f},{-269925.0f,-132402.0f}};
    for (int i=0; i<3; ++i) {
        gevrSkyVertexPosition(&input[i],xy[i][0],xy[i][1],depth[i],st[i][0],st[i][1],0,10,320,220);
        std::memset(input[i].rgba,17+i,4);
        assert(std::fabs((input[i].x/input[i].w+1)*160-xy[i][0]) < 0.0001f);
        assert(std::fabs((1-input[i].y/input[i].w)*110+10-xy[i][1]) < 0.0001f);
    }
    rsp.texture_scaling_factor = {65535,65535};
    rdp.fog_color.a = 23;
    rsp.loaded_vertices[3].w = 12345;
    gfx_sp_sky_vertex(3,input);
    assert(rsp.loaded_vertices[3].w == 12345); // bounded vertex-cache load
    for (int i=0; i<3; ++i) {
        const auto &v=rsp.loaded_vertices[i];
        assert(v.w == depth[i] && v.u == st[i][0] && v.v == st[i][1]);
        assert(v.z == 0 && v.clip_rej == 0 && v.fog == 23);
        assert(v.color.r == 17+i && v.color.a == 17+i);
        assert(v.x == input[i].x + input[i].w*0.1f);
    }
    for (double a : {0.1,0.3,0.7}) for (double b : {0.05,0.1,0.2}) {
        if (a+b >= 1) continue;
        double c=1-a-b;
        for (int t=0; t<2; ++t) {
            double expected=(a*st[0][t]/depth[0]+b*st[1][t]/depth[1]+c*st[2][t]/depth[2])
                /(a/depth[0]+b/depth[1]+c/depth[2]);
            assert(std::fabs(interpolate(rsp.loaded_vertices,a,b,t)-expected) < 0.001);
        }
    }
    rsp.texture_scaling_factor={32767,16383};
    aspect=0.75f;
    gfx_sp_sky_vertex(3,input);
    assert(rsp.loaded_vertices[2].u == st[2][0]*0.5f);
    assert(rsp.loaded_vertices[2].v == st[2][1]*0.25f);
    assert(rsp.loaded_vertices[2].x == (input[2].x+input[2].w*0.1f)*aspect);
    std::puts("water: full-range positions, UVs, perspective, scale, color and aspect passed");
}

static void mul(float out[4][4],const float a[4][4],const float b[4][4]) {
    for(int i=0;i<4;++i)for(int j=0;j<4;++j) {
        out[i][j]=0;
        for(int k=0;k<4;++k)out[i][j]+=a[i][k]*b[k][j];
    }
}
static void reflection() {
    double maxError=0;
    for(int yaw=-170;yaw<=170;yaw+=17)for(int pitch=-70;pitch<=70;pitch+=14)for(int roll=-60;roll<=60;roll+=15) {
        float y=yaw*0.01745329252f,p=pitch*0.01745329252f,r=roll*0.01745329252f;
        float ry[4][4]={{cosf(y),0,sinf(y),0},{0,1,0,0},{-sinf(y),0,cosf(y),0},{0,0,0,1}};
        float rx[4][4]={{1,0,0,0},{0,cosf(p),sinf(p),0},{0,-sinf(p),cosf(p),0},{0,0,0,1}};
        float rz[4][4]={{cosf(r),sinf(r),0,0},{-sinf(r),cosf(r),0,0},{0,0,1,0},{0,0,0,1}};
        float tmp[4][4],view[4][4];mul(tmp,rx,ry);mul(view,tmp,rz);
        view[3][0]=12345;view[3][1]=-9867; // translation must not enter an axis
        for (int axis=0;axis<2;++axis) {
            int8_t dir[3]={0,0,0};dir[axis]=127;
            gevrReflectionAxisToView(dir,view);
            double modelAxis[3]={};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)modelAxis[i]+=view[i][j]*dir[j]/127.0;
            double len=std::sqrt(modelAxis[0]*modelAxis[0]+modelAxis[1]*modelAxis[1]+modelAxis[2]*modelAxis[2]);
            for(int i=0;i<3;++i) {
                double error=std::fabs(modelAxis[i]/len-(i==axis ? 1.0 : 0.0));
                if(error>maxError)maxError=error;
                assert(error<0.015); // 8-bit LookAt quantization, no head-angle sweep
            }
        }
    }
    std::printf("reflection: 2079 yaw/pitch/roll poses passed; max axis error %.6f\n",maxError);
}
int main() { water(); reflection(); }
