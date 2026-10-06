#include "gevr_hud_bounds.h"
#include "glad/glad.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static GLint readFbo=10, drawFbo=20, packBuffer=30, texture=40;
static GLint alignment=8, rowLength=173, skipRows=3, skipPixels=7;
static bool scissor=true, complete=true, failMap, corrupt, failBuffer, blank, failFence;
static int reads, maps, unmaps, buffersMade, buffersDeleted, syncsMade, syncsDeleted;
static int texturesMade, texturesDeleted, fbosMade, fbosDeleted;
static int pixelX=96, pixelY=30;
static std::map<GLuint,std::vector<unsigned char>> buffers;
struct Fence { GLuint buffer; GLenum result=GL_TIMEOUT_EXPIRED; };
static std::map<GLsync,Fence> fences;
static std::map<GLuint,bool> bufferReady;
static void APIENTRY get(GLenum p, GLint *v) {
    switch(p) {
        case GL_READ_FRAMEBUFFER_BINDING:*v=readFbo;break;
        case GL_DRAW_FRAMEBUFFER_BINDING:*v=drawFbo;break;
        case GL_PIXEL_PACK_BUFFER_BINDING:*v=packBuffer;break;
        case GL_TEXTURE_BINDING_2D:*v=texture;break;
        case GL_PACK_ALIGNMENT:*v=alignment;break;
        case GL_PACK_ROW_LENGTH:*v=rowLength;break;
        case GL_PACK_SKIP_ROWS:*v=skipRows;break;
        case GL_PACK_SKIP_PIXELS:*v=skipPixels;break;
        default:assert(false);
    }
}
static void APIENTRY pixelStore(GLenum p, GLint v) {
    switch(p) {
        case GL_PACK_ALIGNMENT:alignment=v;break;
        case GL_PACK_ROW_LENGTH:rowLength=v;break;
        case GL_PACK_SKIP_ROWS:skipRows=v;break;
        case GL_PACK_SKIP_PIXELS:skipPixels=v;break;
        default:assert(false);
    }
}
static GLboolean APIENTRY isEnabled(GLenum p){assert(p==GL_SCISSOR_TEST);return scissor;}
static void APIENTRY enable(GLenum p){assert(p==GL_SCISSOR_TEST);scissor=true;}
static void APIENTRY disable(GLenum p){assert(p==GL_SCISSOR_TEST);scissor=false;}
static void APIENTRY genBuffer(GLsizei n, GLuint *ids){
    for(int i=0;i<n;i++) {ids[i]=failBuffer ? 0 : 100+(++buffersMade);if(ids[i]) buffers[ids[i]]={};}
}
static void APIENTRY bindBuffer(GLenum p, GLuint id){assert(p==GL_PIXEL_PACK_BUFFER);packBuffer=id;}
static void APIENTRY bufferData(GLenum p, GLsizeiptr n, const void *data, GLenum usage){
    assert(p==GL_PIXEL_PACK_BUFFER && n==128*128*4 && !data && usage==GL_STREAM_READ);
    assert(buffers.count(packBuffer));buffers[packBuffer].resize(n);
}
static void APIENTRY deleteBuffer(GLsizei n,const GLuint *ids){
    for(int i=0;i<n;i++) if(ids[i]) {assert(buffers.erase(ids[i])==1);buffersDeleted++;}
}
static void *APIENTRY mapBuffer(GLenum p, GLintptr offset, GLsizeiptr n, GLbitfield flags){
    assert(p==GL_PIXEL_PACK_BUFFER && offset==0 && n==128*128*4 && flags==GL_MAP_READ_BIT);
    assert(bufferReady[packBuffer]); maps++;
    return failMap ? nullptr : buffers[packBuffer].data();
}
static GLboolean APIENTRY unmapBuffer(GLenum p){assert(p==GL_PIXEL_PACK_BUFFER);unmaps++;return !corrupt;}
static GLsync APIENTRY fence(GLenum p,GLbitfield flags){
    assert(p==GL_SYNC_GPU_COMMANDS_COMPLETE && flags==0);
    if(failFence)return nullptr;
    GLsync id=(GLsync)(uintptr_t)(++syncsMade);fences[id]={static_cast<GLuint>(packBuffer),GL_TIMEOUT_EXPIRED};return id;
}
static GLenum APIENTRY wait(GLsync id,GLbitfield flags,GLuint64 timeout){
    assert(flags==0 && timeout==0 && fences.count(id)); /* no flush or blocking wait */
    const auto f=fences[id];bufferReady[f.buffer]=f.result==GL_ALREADY_SIGNALED || f.result==GL_CONDITION_SATISFIED;
    return f.result;
}
static void APIENTRY deleteSync(GLsync id){assert(fences.erase(id)==1);syncsDeleted++;}
static void signal(GLenum result=GL_ALREADY_SIGNALED){for(auto &item:fences)item.second.result=result;}
static void APIENTRY genTexture(GLsizei n,GLuint *ids){for(int i=0;i<n;i++)ids[i]=500+(++texturesMade);}
static void APIENTRY bindTexture(GLenum p,GLuint id){assert(p==GL_TEXTURE_2D);texture=id;}
static void APIENTRY textureImage(GLenum,GLint,GLint,GLsizei w,GLsizei h,GLint,GLenum,GLenum,const void *p){assert(w==128 && h==128 && !p);}
static void APIENTRY textureParameter(GLenum p,GLenum,GLint){assert(p==GL_TEXTURE_2D);}
static void APIENTRY deleteTexture(GLsizei n,const GLuint *){texturesDeleted+=n;}
static void APIENTRY genFramebuffer(GLsizei n,GLuint *ids){for(int i=0;i<n;i++)ids[i]=700+(++fbosMade);}
static void APIENTRY bindFramebuffer(GLenum p,GLuint id){
    if(p==GL_FRAMEBUFFER)readFbo=drawFbo=id;
    else if(p==GL_READ_FRAMEBUFFER)readFbo=id;
    else {assert(p==GL_DRAW_FRAMEBUFFER);drawFbo=id;}
}
static void APIENTRY attach(GLenum p,GLenum a,GLenum target,GLuint id,GLint level){
    assert(p==GL_FRAMEBUFFER && a==GL_COLOR_ATTACHMENT0 && target==GL_TEXTURE_2D && id && level==0);
}
static GLenum APIENTRY check(GLenum p){assert(p==GL_FRAMEBUFFER);return complete ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_UNSUPPORTED;}
static void APIENTRY deleteFramebuffer(GLsizei n,const GLuint *){fbosDeleted+=n;}
static void APIENTRY blit(GLint,GLint,GLint,GLint,GLint,GLint,GLint w,GLint h,GLbitfield p,GLenum filter){
    assert(readFbo==77 && drawFbo>=700 && !scissor && w==128 && h==128 && p==GL_COLOR_BUFFER_BIT && filter==GL_LINEAR);
}
static void APIENTRY read(GLint,GLint,GLsizei w,GLsizei h,GLenum f,GLenum type,void *p){
    assert(!p && buffers.count(packBuffer) && readFbo>=700 && w==128 && h==128);
    assert(f==GL_RGBA && type==GL_UNSIGNED_BYTE && alignment==4 && !rowLength && !skipRows && !skipPixels);
    reads++;bufferReady[packBuffer]=false;
    auto &pixels=buffers[packBuffer];std::memset(pixels.data(),0,pixels.size());
    if(!blank)for(int y=pixelY;y<pixelY+4;y++)for(int x=pixelX;x<pixelX+8;x++)pixels[(y*128+x)*4+3]=255;
}
PFNGLGETINTEGERVPROC glad_glGetIntegerv=get;
PFNGLPIXELSTOREIPROC glad_glPixelStorei=pixelStore;
PFNGLISENABLEDPROC glad_glIsEnabled=isEnabled;
PFNGLENABLEPROC glad_glEnable=enable;
PFNGLDISABLEPROC glad_glDisable=disable;
PFNGLGENBUFFERSPROC glad_glGenBuffers=genBuffer;
PFNGLBINDBUFFERPROC glad_glBindBuffer=bindBuffer;
PFNGLBUFFERDATAPROC glad_glBufferData=bufferData;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers=deleteBuffer;
PFNGLMAPBUFFERRANGEPROC glad_glMapBufferRange=mapBuffer;
PFNGLUNMAPBUFFERPROC glad_glUnmapBuffer=unmapBuffer;
PFNGLFENCESYNCPROC glad_glFenceSync=fence;
PFNGLCLIENTWAITSYNCPROC glad_glClientWaitSync=wait;
PFNGLDELETESYNCPROC glad_glDeleteSync=deleteSync;
PFNGLGENTEXTURESPROC glad_glGenTextures=genTexture;
PFNGLBINDTEXTUREPROC glad_glBindTexture=bindTexture;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D=textureImage;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri=textureParameter;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures=deleteTexture;
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers=genFramebuffer;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer=bindFramebuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D=attach;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus=check;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers=deleteFramebuffer;
PFNGLBLITFRAMEBUFFERPROC glad_glBlitFramebuffer=blit;
PFNGLREADPIXELSPROC glad_glReadPixels=read;

static void update(int aim=0,int width=1680){
    gfx_vr_hud_bounds_update(77,width,1760,aim);
    assert(readFbo==10 && drawFbo==20 && packBuffer==30 && texture==40 && scissor);
    assert(alignment==8 && rowLength==173 && skipRows==3 && skipPixels==7);
}
static void cleanup(){
    gfx_vr_hud_bounds_reset();
    assert(buffersMade==buffersDeleted && syncsMade==syncsDeleted);
    assert(texturesMade==texturesDeleted && fbosMade==fbosDeleted && fences.empty());
}
int main(){
    float box[4];
    glad_glFenceSync=nullptr;update();assert(!reads);glad_glFenceSync=fence;
    update();update();update();update();assert(reads==3 && !maps && !unmaps);
    assert(!gfx_vr_hud_bounds_box(box) && box[0]==0 && box[2]==1);
    signal();update();assert(maps==3 && unmaps==3 && reads==4);
    assert(gfx_vr_hud_bounds_box(box) && box[0]==96.f/128 && box[3]==34.f/128);
    /* Pending normal-aim measurements must not crop a new aim layout. */
    pixelX=104;signal();const int before=maps;update(1);assert(maps==before);
    assert(!gfx_vr_hud_bounds_box(box) && box[2]==1);
    signal(GL_CONDITION_SATISFIED);update(1);
    assert(gfx_vr_hud_bounds_box(box) && box[0]==104.f/128);
    update(0);assert(gfx_vr_hud_bounds_box(box) && box[0]==96.f/128);
    signal();update(0,1440);assert(!gfx_vr_hud_bounds_box(box));
    failMap=true;signal();update(0,1440);assert(!gfx_vr_hud_bounds_box(box));failMap=false;
    corrupt=true;signal();update(0,1440);assert(!gfx_vr_hud_bounds_box(box));corrupt=false;
    signal();update(0,1440);assert(gfx_vr_hud_bounds_box(box));
    signal(GL_WAIT_FAILED);const int failedMaps=maps;update(0,1440);assert(maps==failedMaps);
    cleanup();complete=false;const int beforeFailure=reads;update();assert(reads==beforeFailure);cleanup();complete=true;
    failBuffer=true;update();assert(reads==beforeFailure);cleanup();failBuffer=false;
    blank=true;update();signal();update();assert(!gfx_vr_hud_bounds_box(box));cleanup();
    blank=false;failFence=true;const int beforeFenceReads=reads;update();assert(reads==beforeFenceReads+1);
    assert(fences.empty());cleanup();failFence=false;
    puts("PASS: production HUD readback uses PBOs, polls with zero timeout, never maps pending transfers, restores GL state, handles aim/resize/full-ring/failures and cleans up");
}
