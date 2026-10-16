#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "net/net_protocol.h"

void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
static void near(float a,float b) {assert(isfinite(a)&&isfinite(b)&&fabsf(a-b)<.002f);}
static float distance(const float *a,const float *b) {float d=0;for(int k=0;k<3;k++)d+=(a[k]-b[k])*(a[k]-b[k]);return sqrtf(d);}
static GevrAvatarPose identity(void) {
    GevrAvatarPose p={0};p.flags=7;p.head.q[3]=p.hand[0].q[3]=p.hand[1].q[3]=1;
    p.hand[0].pos[0]=20;p.hand[1].pos[0]=-20;return p;
}
static void arms(void) {
    float shoulder[3]={10,100,30},pole[3]={1,-1,-.3f},prev[3]={0,-1,0},el[3],wr[3];
    const float targets[][3]={{30,70,50},{10,100,30},{10000,100,30},{-10000,100,30},{10,90,30},{10,100,30.00001f}};
    for(unsigned i=0;i<sizeof(targets)/sizeof(*targets);i++)for(int side=0;side<2;side++) {
        pole[0]=side?1:-1;
        assert(gevrArmSolve(shoulder,targets[i],30,25,pole,prev,el,wr));
        near(distance(shoulder,el),30);near(distance(el,wr),25);
        if(i==0)near(distance(wr,targets[i]),0);
    }
    float equal[3]={0,0,0},parallel[3]={0,-1,0};
    assert(gevrArmSolve(equal,equal,30,30,parallel,parallel,el,wr));
    near(distance(equal,el),30);near(distance(el,wr),30);
    assert(!gevrArmSolve(equal,equal,0,30,pole,prev,el,wr));
    equal[0]=NAN;assert(!gevrArmSolve(equal,equal,30,30,pole,prev,el,wr));
    /* Reach sweep across the body: finite results and fixed segment lengths. */
    for(int i=0;i<5000;i++) {
        float t=i*.013f,target[3]={10+60*sinf(t),100+60*cosf(t*2),30+60*sinf(t*3)};
        assert(gevrArmSolve(shoulder,target,30,25,pole,prev,el,wr));
        near(distance(shoulder,el),30);near(distance(el,wr),25);
    }
}
static void rotations(void) {
    for(int i=0;i<500;i++) {
        float q[4]={sinf(i*.1f),cosf(i*.2f),sinf(i*.3f),cosf(i*.4f)},m[3][3],q2[4],n[3][3];
        float d=0;for(int k=0;k<4;k++)d+=q[k]*q[k];for(int k=0;k<4;k++)q[k]/=sqrtf(d);
        gevrPoseQuatMatrix(q,m);gevrPoseMatrixQuat(m,q2);gevrPoseQuatMatrix(q2,n);
        for(int r=0;r<3;r++)for(int k=0;k<3;k++)near(m[r][k],n[r][k]);
        for(int k=0;k<4;k++)q2[k]=-q[k];
        gevrPoseQuatLerp(q,q2,.5f,q2);gevrPoseQuatMatrix(q2,n);
        for(int r=0;r<3;r++)for(int k=0;k<3;k++)near(m[r][k],n[r][k]);
    }
}
static void protocol(void) {
    uint8_t raw[256];struct netbuf b={.data=raw,.size=sizeof(raw)};
    struct netplayermove m={0},r={0};m.avatar=identity();m.tick=123;m.epoch=123456;m.life_id=8;
    m.avatar.flags|=GEVR_POSE_LEFT_HANDED;m.avatar.head.pos[1]=25;m.avatar.hand[0].q[2]=.6f;m.avatar.hand[0].q[3]=.8f;
    m.avatar.body_yaw=-179;m.aimdir.z=1;m.weaponnum=4;
    /* The real 8-byte transport header plus the real serializer fits 256 bytes. */
    netbufStartWrite(&b);netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_PLAYER_STATE);netbufWriteU8(&b,3);netbufWritePlayerMove(&b,&m);
    assert(!b.error && b.wp==253);unsigned total=b.wp;
    netbufStartReadData(&b,raw+8,total-8);netbufReadPlayerMove(&b,&r);
    assert(!b.error && !netbufReadLeft(&b));assert(r.avatar.flags==15 && r.tick==123 && r.life_id==8);
    near(r.avatar.head.pos[1],25);near(r.avatar.hand[0].q[2],.6f);near(r.avatar.hand[1].pos[0],-20);
    for(unsigned size=0;size<total-8;size++) {
        netbufStartReadData(&b,raw+8,size);netbufReadPlayerMove(&b,&r);assert(b.error);
    }
    m.avatar.hand[1].q[0]=NAN;
    b.data=raw;b.size=sizeof(raw);netbufStartWrite(&b);netbufWritePlayerMove(&b,&m);
    netbufStartReadData(&b,raw,b.wp);netbufReadPlayerMove(&b,&r);assert(b.error);
    GevrAvatarPose p=identity();p.flags=0;assert(gevrPoseValidate(&p)); /* bots/flat */
    p=identity();p.hand[0].pos[0]=1000;assert(!gevrPoseValidate(&p));
    p=identity();memset(p.head.q,0,sizeof(p.head.q));assert(!gevrPoseValidate(&p));
    p=identity();p.flags=16;assert(!gevrPoseValidate(&p));
    p=identity();p.flags=2;assert(!gevrPoseValidate(&p));
}
static void history(void) {
    GevrPoseHistory h={0};GevrAvatarPose p=identity(),out;float anchor[3]={0},w[3];
    p.body_yaw=179;gevrPosePush(&h,&p,anchor,1,1,1000000);
    p.hand[0].pos[0]=40;p.body_yaw=-179;gevrPosePush(&h,&p,anchor,1,1,1100000);
    assert(gevrPoseSample(&h,1100000,&out,w));near(out.hand[0].pos[0],30);near(fabsf(out.body_yaw),180);
    /* One lost controller fades independently while the other keeps tracking. */
    p.flags=GEVR_POSE_HEAD|GEVR_POSE_LEFT;
    gevrPosePush(&h,&p,anchor,1,1,1250000);
    assert(gevrPoseSample(&h,1300000,&out,w));near(w[1],.5f);near(w[2],1);
    assert(!gevrPoseSample(&h,1600000,&out,w));
    /* Death/respawn/round/teleport don't interpolate through an old pose. */
    gevrPosePush(&h,&p,anchor,1,2,1650000);assert(h.count==1);
    gevrPosePush(&h,&p,anchor,2,2,1660000);assert(h.count==1);
    anchor[0]=300;gevrPosePush(&h,&p,anchor,2,2,1670000);assert(h.count==1);
    p.flags|=GEVR_POSE_LEFT_HANDED;gevrPosePush(&h,&p,anchor,2,2,1680000);assert(h.count==1);
    for(int i=0;i<30;i++)gevrPosePush(&h,&p,anchor,2,2,1690000+i*16000);
    assert(h.count==GEVR_POSE_HISTORY);
    gevrPosePush(&h,&p,anchor,2,2,h.time[h.count-1]);assert(h.count==GEVR_POSE_HISTORY);
    /* An untracked start carries no yaw: neither a NaN nor a 0 reaches the torso. */
    GevrPoseHistory fresh={0};GevrAvatarPose flat={0};
    flat.body_yaw=NAN;flat.head.pos[0]=NAN;assert(gevrPoseValidate(&flat) && flat.body_yaw==0 && flat.head.pos[0]==0);
    flat.body_yaw=NAN;gevrPosePush(&fresh,&flat,anchor,3,1,2000000);
    p=identity();p.body_yaw=90;gevrPosePush(&fresh,&p,anchor,3,1,2040000);
    assert(gevrPoseSample(&fresh,2070000,&out,w));near(out.body_yaw,90);
}
int main(void) {arms();rotations();protocol();history();puts("avatar: solver, rotations, protocol, interpolation/reset checks passed");return 0;}
