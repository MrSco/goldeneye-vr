"""Compile production Quest mapping and sight draw against synthetic input."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
inp = (root / 'port/src/input.c').read_text()
axis = inp[inp.index('static inline s32 inputAxisScale('):inp.index('s32 inputReadController(')]
mapping = inp[inp.index('    /* Quest screen mode:'):inp.index('static inline void inputUpdateMouse(')]
gun = (root / 'src/game/gunfire.c').read_text()
sight = gun[gun.index('void gunDrawSight('):gun.index('void inc_curplayer_hitcount')]
stub = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
using s32=int32_t; using f32=float;
struct XrVector2f { float x,y; } sticks[2];
struct OSContPad { uint16_t button; int8_t stick_x,stick_y,rstick_x,rstick_y; };
struct Config { int deadzone[2]={4096,4096}; int axisMap[1][2]={{0,1}}; float sens[2]={1,1}; } config;
struct Player { int pause_state, gunsightmode, mpmenuon; struct {float f[2];} crosshair_angle; } player;
Player *g_CurrentPlayer=&player;
int stage=1;
#define LEVELID_TITLE 0
#define FALSE 0
#define SCREEN_RATIO_16_9 1
#define Z_TRIG 0x2000
#define A_BUTTON 0x8000
#define B_BUTTON 0x4000
#define START_BUTTON 0x1000
#define R_TRIG 0x10
#define L_CBUTTONS 2
#define R_CBUTTONS 1
#define U_CBUTTONS 8
#define D_CBUTTONS 4
std::map<std::string,bool> buttons[2];
bool get_button_state(int h,const char *b) { return buttons[h][b]; }
bool get_2d_input(int h,const char*,XrVector2f *v) { *v=sticks[h]; return true; }
int bossGetStageNum() { return stage; }
using Gfx=uint64_t;
struct Image {int level;} crosshair;
Image *crosshairimage=&crosshair;
Gfx *expected;
void texSelect(Gfx **p,Image*,int,int,int) { assert(*p==expected); ++*p; }
void display_image_at_position(Gfx **p,float*,float*,int,int,int,int,int,int,int,int,int,int,int) { ++*p; }
int get_screen_ratio() {return 0;}
'''
tests = r'''
int main() {
 OSContPad p{};
 sticks[0]={1,1}; sticks[1]={-1,0.5f};
 apply(&p); assert(p.button==(R_CBUTTONS|U_CBUTTONS)); assert(p.stick_x<0 && p.stick_y>0);
 assert(!p.rstick_x && !p.rstick_y);
 buttons[1]["trigger"]=buttons[1]["b"]=buttons[0]["grip"]=true;
 apply(&p); assert((p.button&(Z_TRIG|B_BUTTON|R_TRIG))==(Z_TRIG|B_BUTTON|R_TRIG));
 buttons[0].clear(); buttons[1].clear(); sticks[0]=sticks[1]={0,0};
 apply(&p); assert(!p.button && !p.stick_x && !p.stick_y);
 buttons[0]["thumbstick_click"]=buttons[1]["thumbstick_click"]=true;
 apply(&p); assert(!p.button);
 stage=LEVELID_TITLE; sticks[0]={1,-1}; apply(&p);
 assert(!p.button && p.stick_x>0 && p.stick_y<0);
 stage=1; player.pause_state=1; apply(&p); assert(!p.button && p.stick_x>0);
 buttons[0]["menu"]=true; apply(&p); assert(p.button==START_BUTTON);
 auto *dl=new Gfx[8]; assert(reinterpret_cast<uintptr_t>(dl)>UINT32_MAX);
 expected=dl; Gfx *cursor=dl; gunDrawSight(&cursor); assert(cursor==dl+2);
 player.gunsightmode=1; gunDrawSight(&cursor); assert(cursor==dl+2); delete[] dl;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-controls-') as tmp:
    tmp = Path(tmp)
    code = tmp / 'controls.cpp'
    code.write_text(stub + axis + 'int apply(OSContPad *npad) { int idx=0; auto *cfg=&config;\n' + mapping + sight + tests)
    exe = tmp / ('controls.exe' if os.name == 'nt' else 'controls')
    compiler = r'C:\Strawberry\c\bin\g++.exe' if os.name == 'nt' else 'c++'
    subprocess.run([compiler, '-std=c++20', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: gameplay/menu mapping, releases, stick clicks, and 64-bit aim display-list cursor')
