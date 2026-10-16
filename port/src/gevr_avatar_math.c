/* Analytic two-bone geometry follows PD CE's wound-clutch solve:
 * Chesluk-Industries/Perfect-Dark-CE e985acc, ragdoll/motion.c:856-895 (MIT).
 * Network interpolation, validation and degeneracy handling are GoldenEye VR's. */
#include "gevr_avatar_math.h"
#include <math.h>
#include <string.h>

static float dot3(const float *a, const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static int norm(float *v, int n) {
    float d=0; for(int i=0;i<n;i++)d+=v[i]*v[i];
    if(!isfinite(d) || d<1e-10f)return 0;
    d=1/sqrtf(d); for(int i=0;i<n;i++)v[i]*=d; return 1;
}
static float clamp(float x,float lo,float hi) { return fmaxf(lo,fminf(x,hi)); }
static float angle(float d) { return remainderf(d,360.0f); }

int gevrPoseValidate(GevrAvatarPose *p) {
    if(p->flags & ~15u)return 0;
    if(!(p->flags & GEVR_POSE_HEAD) && (p->flags & 6u))return 0;
    /* Untracked parts carry no data: clear them so nothing unchecked
     * (NaN included) reaches the history or the renderer. */
    if(!(p->flags & GEVR_POSE_HEAD))p->body_yaw=0;
    if(!isfinite(p->body_yaw) || fabsf(p->body_yaw)>3600)return 0;
    for(int j=0;j<3;j++) {
        GevrTrackedPose *t=j?&p->hand[j-1]:&p->head;
        if(!(p->flags & (1u<<j))) {memset(t,0,sizeof(*t));t->q[3]=1;continue;}
        float d=0;
        for(int k=0;k<3;k++) {
            if(!isfinite(t->pos[k]))return 0;
            d+=t->pos[k]*t->pos[k];
        }
        if(d>(j?250.0f*250.0f:120.0f*120.0f))return 0;
        float qd=0;for(int k=0;k<4;k++)qd+=t->q[k]*t->q[k];
        if(!isfinite(qd) || qd<0.5f || qd>1.5f || !norm(t->q,4))return 0;
    }
    p->body_yaw=angle(p->body_yaw);
    return 1;
}

void gevrPoseQuatLerp(const float a[4],const float b[4],float t,float out[4]) {
    float d=0;for(int k=0;k<4;k++)d+=a[k]*b[k];
    float sign=d<0?-1.0f:1.0f;
    for(int k=0;k<4;k++)out[k]=a[k]*(1-t)+sign*b[k]*t;
    if(!norm(out,4)) { memset(out,0,4*sizeof(float));out[3]=1; }
}
void gevrPoseQuatMatrix(const float q[4],float m[3][3]) {
    float x=q[0],y=q[1],z=q[2],w=q[3];
    m[0][0]=1-2*(y*y+z*z);m[0][1]=2*(x*y+w*z);m[0][2]=2*(x*z-w*y);
    m[1][0]=2*(x*y-w*z);m[1][1]=1-2*(x*x+z*z);m[1][2]=2*(y*z+w*x);
    m[2][0]=2*(x*z+w*y);m[2][1]=2*(y*z-w*x);m[2][2]=1-2*(x*x+y*y);
}
void gevrPoseMatrixQuat(const float m[3][3],float q[4]) {
    float t=m[0][0]+m[1][1]+m[2][2];
    if(t>0) {
        float s=sqrtf(t+1)*2;
        q[3]=s/4;q[0]=(m[1][2]-m[2][1])/s;q[1]=(m[2][0]-m[0][2])/s;q[2]=(m[0][1]-m[1][0])/s;
    } else {
        int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;
        int j=(i+1)%3,k=(i+2)%3;
        float s=sqrtf(fmaxf(0,1+m[i][i]-m[j][j]-m[k][k]))*2;
        if(s<1e-6f) { memset(q,0,4*sizeof(float));q[3]=1;return; }
        q[i]=s/4;q[j]=(m[i][j]+m[j][i])/s;q[k]=(m[i][k]+m[k][i])/s;
        q[3]=(m[j][k]-m[k][j])/s;
    }
    norm(q,4);
}

void gevrPosePush(GevrPoseHistory *h,const GevrAvatarPose *input,const float anchor[3],
                  uint64_t epoch,uint32_t life,uint64_t now) {
    GevrAvatarPose p=*input;
    float delta[3];for(int k=0;k<3;k++)delta[k]=anchor[k]-h->anchor[k];
    if(h->epoch!=epoch || h->life!=life || (h->count &&
       (now<h->time[h->count-1] || now-h->time[h->count-1]>500000 || dot3(delta,delta)>10000 ||
        ((p.flags^h->pose[h->count-1].flags)&GEVR_POSE_LEFT_HANDED))))memset(h,0,sizeof(*h));
    h->epoch=epoch;h->life=life;memcpy(h->anchor,anchor,sizeof(h->anchor));
    if(!gevrPoseValidate(&p))memset(&p,0,sizeof(p));
    for(int j=0;j<3;j++) {
        if(p.flags & (1u<<j))h->valid_time[j]=now;
        else if(h->count) {
            const GevrAvatarPose *last=&h->pose[h->count-1];
            if(j)p.hand[j-1]=last->hand[j-1];else {p.head=last->head;p.body_yaw=last->body_yaw;}
        }
    }
    if(h->count && h->time[h->count-1]==now) {h->pose[h->count-1]=p;return;}
    if(h->count==GEVR_POSE_HISTORY) {
        memmove(h->pose,h->pose+1,(GEVR_POSE_HISTORY-1)*sizeof(*h->pose));
        memmove(h->time,h->time+1,(GEVR_POSE_HISTORY-1)*sizeof(*h->time));h->count--;
    }
    h->pose[h->count]=p;h->time[h->count++]=now;
}
int gevrPoseSample(const GevrPoseHistory *h,uint64_t now,GevrAvatarPose *out,float weight[3]) {
    memset(weight,0,3*sizeof(float));
    if(!h->count || now<h->time[h->count-1])return 0;
    uint64_t at=now>50000?now-50000:0; /* 50 ms presentation delay, local receive timeline */
    int a=0;while(a+1<h->count && h->time[a+1]<=at)a++;
    int b=a+1<h->count?a+1:a;
    float t=a==b?0:clamp((float)(at>h->time[a]?at-h->time[a]:0)/(h->time[b]-h->time[a]),0,1);
    *out=h->pose[a];
    for(int j=0;j<3;j++) {
        const GevrTrackedPose *pa=j?&h->pose[a].hand[j-1]:&h->pose[a].head;
        const GevrTrackedPose *pb=j?&h->pose[b].hand[j-1]:&h->pose[b].head;
        GevrTrackedPose *o=j?&out->hand[j-1]:&out->head;
        /* First acquisition has no valid endpoint to interpolate from. */
        if(!(h->pose[a].flags & (1u<<j)) && (h->pose[b].flags & (1u<<j)))pa=pb;
        for(int k=0;k<3;k++)o->pos[k]=pa->pos[k]*(1-t)+pb->pos[k]*t;
        gevrPoseQuatLerp(pa->q,pb->q,t,o->q);
        if(h->valid_time[j] && now>=h->valid_time[j])
            weight[j]=clamp(1-((float)(now-h->valid_time[j])-100000)/200000,0,1);
    }
    /* The torso yaw rides with the head: no interpolation from an untracked start. */
    float ya=h->pose[b].body_yaw;
    if((h->pose[a].flags & GEVR_POSE_HEAD) || !(h->pose[b].flags & GEVR_POSE_HEAD))ya=h->pose[a].body_yaw;
    out->body_yaw=angle(ya+angle(h->pose[b].body_yaw-ya)*t);
    out->flags=h->pose[h->count-1].flags;
    return weight[0]>0;
}

int gevrArmSolve(const float shoulder[3],const float target[3],float upper,float lower,
                 const float pole[3],const float previous[3],float elbow[3],float wrist[3]) {
    if(!isfinite(upper) || !isfinite(lower) || upper<0.01f || lower<0.01f)return 0;
    float u[3],n[3],d=0;
    for(int k=0;k<3;k++) {if(!isfinite(shoulder[k]) || !isfinite(target[k]))return 0;u[k]=target[k]-shoulder[k];d+=u[k]*u[k];}
    d=sqrtf(d);
    if(!norm(u,3)) {u[0]=0;u[1]=-1;u[2]=0;}
    d=clamp(d,fabsf(upper-lower)+0.001f,(upper+lower)*0.9999f);
    float a=(upper*upper-lower*lower+d*d)/(2*d),h=sqrtf(fmaxf(0,upper*upper-a*a));
    float dot=dot3(pole,u);for(int k=0;k<3;k++)n[k]=pole[k]-u[k]*dot;
    if(!norm(n,3)) {
        dot=dot3(previous,u);for(int k=0;k<3;k++)n[k]=previous[k]-u[k]*dot;
        if(!norm(n,3)) {
            int axis=fabsf(u[0])<fabsf(u[1])?0:1;if(fabsf(u[2])<fabsf(u[axis]))axis=2;
            for(int k=0;k<3;k++)n[k]=(k==axis?1.0f:0)-u[k]*u[axis];norm(n,3);
        }
    }
    for(int k=0;k<3;k++) {elbow[k]=shoulder[k]+u[k]*a+n[k]*h;wrist[k]=shoulder[k]+u[k]*d;}
    return 1;
}
