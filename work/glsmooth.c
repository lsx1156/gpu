/* glsmooth.c - minimal GLES2 smooth color-varying triangle, ONLY for fd5 RD reference.
 * Purpose (H2b): capture a CLEAN per-draw FS-varying reference from working gallium fd5,
 * then diff against tu5xx fresh_varying_00001.rd to find the per-draw FS varying
 * storage/base delivery that tu misses.
 * - ESL surfaceless + pbuffer 128x128, ES2
 * - VS outputs vec3 smooth varying vColor, FS reads it directly -> gl_FragColor
 * - triangle covers center; readback center should be mix of 3 vertex colors
 * - all GL/EGL symbols via dlopen/dlsym, no header dependency, cross-compilable
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef int EGLint;
typedef unsigned EGLBoolean;
typedef unsigned EGLenum;
typedef void (*GLFN)(void);

#define EGL_SURFACE_TYPE   0x3033
#define EGL_PBUFFER_BIT    0x0001
#define EGL_RED_SIZE  0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_NONE      0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT  0x0004
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PBUFFER_WIDTH 0x3050
#define EGL_PBUFFER_HEIGHT 0x3051
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_PLATFORM_X11_KHR 0x31D5
#define EGL_OPENGL_ES3_BIT  0x0040
#define EGL_EXTENSIONS 0x3055
#define EGL_WINDOW_BIT 0x0004
#define EGL_PLATFORM_GBM_MESA 0x31D6
#define EGL_NO_SURFACE ((void*)0)

/* ---- GLES2 constants ---- */
enum { GL_FLOAT=0x1406, GL_CMD_TRIANGLES=0x4, GL_FALSE=0,
       GL_COLOR_BUFFER_BIT=0x4000, GL_UNSIGNED_BYTE=0x1401,
       GL_VERTEX_SHADER=0x8B31, GL_FRAGMENT_SHADER=0x8B30,
       GL_COMPILE_STATUS=0x8B81, GL_LINK_STATUS=0x8B82,
       GL_ARRAY_BUFFER=0x8892, GL_STATIC_DRAW=0x88E4,
       GL_RGBA=0x1908, GL_NO_ERROR=0, GL_FRAMEBUFFER=0x8D40,
       GL_READ_FRAMEBUFFER=0x8CA8 };

int main(void){
    alarm(60);
    void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    void* gls = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    if(!egl || !gls){ printf("dlopen FAIL egl=%p gl=%p (%s)\n", egl, gls, dlerror()); return 1; }
    void* (*gpa)(const char*) = (void*(*)(const char*))dlsym(egl,"eglGetProcAddress");
    EGLDisplay (*gpd)(unsigned,void*,const EGLint*) = (void*)(EGLDisplay(*)(unsigned,void*,const EGLint*))gpa("eglGetPlatformDisplayEXT");
    if(!gpd){ printf("no eglGetPlatformDisplayEXT\n"); return 1; }

    EGLDisplay (*ini)(EGLDisplay,EGLint*,EGLint*) = (void*)(EGLDisplay(*)(EGLDisplay,EGLint*,EGLint*))gpa("eglInitialize");
    void* (*chc)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*) = (void*)(void*(*)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*))gpa("eglChooseConfig");
    void* (*bapi)(EGLenum) = (void*)(void*(*)(EGLenum))gpa("eglBindAPI");
    void* (*mkctx)(EGLDisplay,EGLConfig,EGLContext,const EGLint*) = (void*)(void*(*)(EGLDisplay,EGLConfig,EGLContext,const EGLint*))gpa("eglCreateContext");
    void* (*mksf)(EGLDisplay,EGLConfig,const EGLint*) = (void*)(void*(*)(EGLDisplay,EGLConfig,const EGLint*))gpa("eglCreatePbufferSurface");
    void* (*mkcur)(EGLDisplay,EGLSurface,EGLSurface,EGLContext) = (void*)(void*(*)(EGLDisplay,EGLSurface,EGLSurface,EGLContext))gpa("eglMakeCurrent");
    void* (*winsurf)(EGLDisplay,EGLConfig,void*,const EGLint*) = (void*)(void*(*)(EGLDisplay,EGLConfig,void*,const EGLint*))gpa("eglCreateWindowSurface");
    void* (*swp)(EGLDisplay,EGLSurface) = (void*)(void*(*)(EGLDisplay,EGLSurface))gpa("eglSwapBuffers");
    if(!ini||!chc||!bapi||!mkctx||!mksf||!mkcur||!winsurf){ printf("missing egl fn\n"); return 1; }

    /* ---- EGL setup: try surfaceless -> GBM -> device, ES3 then ES2 ---- */
    EGLDisplay d = 0; EGLContext c = 0; EGLSurface s = 0;
    void* gbmdev = 0; void* gbuf = 0;
    unsigned plat_ok = 0;
    /* ES3 then ES2 context attrs */
    EGLint ca3[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
    EGLint ca2[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE };
    EGLint pa[]  = { EGL_PBUFFER_WIDTH, 128, EGL_PBUFFER_HEIGHT, 128, EGL_NONE };

    for (int pt = 0; pt < 3 && !plat_ok; pt++) {
        EGLDisplay dd = 0;
        unsigned platform = 0; void* native = 0; const char* pname = "?";
        void* (*XOpenDisplay)(const char*) = 0;
        void* (*XDefaultScreen)(void*) = 0;
        void* (*XRootWin)(void*) = 0;
        void* (*XCreateSimp)(void*, unsigned long, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long) = 0;
        void* (*XMapWin)(void*, unsigned long) = 0;
        void* xd = 0; void* xwin = 0;
        if (pt == 0) {
            void* xl = dlopen("libX11.so.6", RTLD_NOW);
            if (xl) {
                XOpenDisplay = (void*(*)(const char*))dlsym(xl, "XOpenDisplay");
                XDefaultScreen = (void*(*)(void*))dlsym(xl, "XDefaultScreen");
                XRootWin = (void*(*)(void*))dlsym(xl, "XDefaultRootWindow");
                XCreateSimp = (void*(*)(void*,unsigned long,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long))dlsym(xl, "XCreateSimpleWindow");
                XMapWin = (void*(*)(void*,unsigned long))dlsym(xl, "XMapWindow");
            }
            if (XOpenDisplay && XCreateSimp && XRootWin) {
                xd = XOpenDisplay(":0");
                if (xd) {
                    platform = EGL_PLATFORM_X11_KHR; pname = "X11";
                    native = xd;
                    xwin = XCreateSimp(xd, (unsigned long)XRootWin(xd), 0, 0, 128, 128, 0, 0, 0);
                    if (XMapWin) XMapWin(xd, (unsigned long)xwin);
                    printf("[X11] win=%p\n", xwin);
                }
            }
            if (!xd) continue;
        }
        else if (pt == 1) {
            int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
            void* (*gbmcd)(int) = (void*(*)(int))dlsym(RTLD_DEFAULT, "gbm_create_device");
            void* (*gbmsc)(void*,unsigned,unsigned,unsigned,unsigned) = 0;
            if (!gbmcd) { void* g = dlopen("libgbm.so.1", RTLD_NOW); if (g) { gbmcd = dlsym(g, "gbm_create_device"); gbmsc = dlsym(g, "gbm_surface_create"); } }
            else gbmsc = (void*(*)(void*,unsigned,unsigned,unsigned,unsigned))dlsym(RTLD_DEFAULT, "gbm_surface_create");
            if (gbmcd && gbmsc && fd >= 0) {
                void* gdev = gbmcd(fd);
                platform = EGL_PLATFORM_GBM_MESA; pname = "GBM"; native = gdev;
                gbmdev = gdev;
                /* gbm_surface_create(gdev,128,128,GBM_FORMAT_XRGB8888,GBM_BO_USE_RENDERING) */
                gbuf = gbmsc(gdev, 128, 128, 0x34325258u, 1u);
                printf("[GBM] gdev=%p gsurf=%p\n", gdev, gbuf);
            } else { if(fd>=0) close(fd); continue; }
        } else { pname = "surf-null"; }

        dd = gpd(platform, native, 0);
        if (!dd) { printf("[%s] no display\n", pname); continue; }
        EGLint Mm = 0, Mn = 0;
        if (!ini(dd, &Mm, &Mn)) { printf("[%s] init fail\n", pname); continue; }
        printf("[%s] display init %d.%d\n", pname, Mm, Mn);
        EGLint ca[] = { EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,
                        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT|EGL_OPENGL_ES3_BIT, EGL_NONE };
        EGLConfig cfgs[8]; EGLint n = 0;
        if (!chc(dd, ca, cfgs, 8, &n) || n == 0) { printf("[%s] no config\n", pname); continue; }
        /* try ES3 then ES2 */
        EGLContext cc = mkctx(dd, cfgs[0], 0, ca3);
        const char* ver = cc ? "ES3" : (cc = mkctx(dd, cfgs[0], 0, ca2), "ES2");
        if (!cc) { printf("[%s] ctx fail\n", pname); continue; }
        EGLSurface ss = 0;
        if (xwin) ss = winsurf(dd, cfgs[0], xwin, NULL);
        else if (gbuf) ss = winsurf(dd, cfgs[0], gbuf, NULL);
        else ss = mksf(dd, cfgs[0], pa);
        printf("[%s] ctx=%s surf=%p\n", pname, ver, ss);
        if (mkcur(dd, ss?ss:EGL_NO_SURFACE, ss?ss:EGL_NO_SURFACE, cc)) { d = dd; c = cc; s = ss; plat_ok = 1; }
    }
    if (!plat_ok) { printf("no EGL platform worked\n"); return 1; }
    printf("GL context OK\n");

    /* ---- GLES2 fns ---- */
    void* (*vp)(EGLint,EGLint,EGLint,EGLint) = (void*)(void*(*)(EGLint,EGLint,EGLint,EGLint))gpa("glViewport");
    void* (*cc)(float,float,float,float) = (void*)(void*(*)(float,float,float,float))gpa("glClearColor");
    void* (*cl)(EGLint) = (void*)(void*(*)(EGLint))gpa("glClear");
    void* (*gbo)(EGLint,void**) = (void*)(void*(*)(EGLint,void**))gpa("glGenBuffers");
    void* (*bb)(EGLenum,void*) = (void*)(void*(*)(EGLenum,void*))gpa("glBindBuffer");
    void* (*bd)(EGLenum,EGLint,const void*,EGLenum) = (void*)(void*(*)(EGLenum,EGLint,const void*,EGLenum))gpa("glBufferData");
    void* (*csh)(EGLenum) = (void*)(void*(*)(EGLenum))gpa("glCreateShader");
    void* (*ssh)(void*,EGLint,const char*const*,const EGLint*) = (void*)(void*(*)(void*,EGLint,const char*const*,const EGLint*))gpa("glShaderSource");
    void* (*cp)(void*) = (void*)(void*(*)(void*))gpa("glCompileShader");
    void* (*gsi)(void*,EGLenum,EGLint*) = (void*)(void*(*)(void*,EGLenum,EGLint*))gpa("glGetShaderiv");
    void* (*gsl)(void*,EGLenum) = (void*)(void*(*)(void*,EGLenum))gpa("glGetShaderInfoLog");
    void* (*cpr)(void) = (void*)(void*(*)(void))gpa("glCreateProgram");
    void* (*at)(void*,void*) = (void*)(void*(*)(void*,void*))gpa("glAttachShader");
    void* (*lk)(void*) = (void*)(void*(*)(void*))gpa("glLinkProgram");
    void* (*gpi)(void*,EGLenum,EGLint*) = (void*)(void*(*)(void*,EGLenum,EGLint*))gpa("glGetProgramiv");
    void* (*use)(void*) = (void*)(void*(*)(void*))gpa("glUseProgram");
    EGLint (*gal)(void*,const char*) = (EGLint(*)(void*,const char*))gpa("glGetAttribLocation");
    void* (*vap)(EGLint,EGLint,EGLenum,EGLBoolean,EGLint,const void*) = (void*)(void*(*)(EGLint,EGLint,EGLenum,EGLBoolean,EGLint,const void*))gpa("glVertexAttribPointer");
    void* (*evaa)(EGLint) = (void*)(void*(*)(EGLint))gpa("glEnableVertexAttribArray");
    void* (*da)(EGLenum,EGLint,EGLenum,const void*) = (void*)(void*(*)(EGLenum,EGLint,EGLenum,const void*))gpa("glDrawArrays");
    void* (*rpx)(EGLint,EGLint,EGLint,EGLint,EGLenum,EGLenum,void*) = (void*)(void*(*)(EGLint,EGLint,EGLint,EGLint,EGLenum,EGLenum,void*))gpa("glReadPixels");
    void* (*ge)(void) = (void*)(void*(*)(void))gpa("glGetError");

    /* ---- shaders ---- */
    const char* vsrc =
        "attribute vec2 aPos;"
        "varying vec3 vColor;"
        "void main(){"
        "  gl_Position=vec4(aPos,0.0,1.0);"
        "  vColor=vec3((aPos.x+1.0)*0.5,0.0,(aPos.y+1.0)*0.5);"
        "}";
    const char* fsrc =
        "precision mediump float;"
        "varying vec3 vColor;"
        "void main(){ gl_FragColor=vec4(vColor,1.0); }";
    void* vs = csh(GL_VERTEX_SHADER);
    void* fs = csh(GL_FRAGMENT_SHADER);
    ssh(vs,1,&vsrc,0); cp(vs);
    ssh(fs,1,&fsrc,0); cp(fs);
    void* prog = cpr();
    at(prog,vs); at(prog,fs); lk(prog);
    use(prog);
    EGLint st=0; gpi(prog,GL_LINK_STATUS,&st);
    printf("link=0x%x\n", st);

    /* ---- triangle covering center: v0(-.5,-.5)c=R, v1(.5,-.5)c=0, v2(0,.5)c=B ---- */
    float vtx[18] = {
        -0.5f, 0.5f,    0.5f,-0.5f,    0.0f, 0.5f
    };
    void* ubo; gbo(1,&ubo); bb(GL_ARRAY_BUFFER,ubo);
    bd(GL_ARRAY_BUFFER, sizeof vtx, vtx, GL_STATIC_DRAW);
    EGLint loc = gal(prog,"aPos");
    vap(loc,2,GL_FLOAT,GL_FALSE,0,0);
    evaa(loc);

    vp(0,0,128,128);
    cc(0,0,0,1); cl(GL_COLOR_BUFFER_BIT);
    da(GL_CMD_TRIANGLES,0,3,0);

    unsigned char pix[4]={0};
    if (s) {
        rpx(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pix);
        unsigned e = (unsigned)(ge?ge():0);
        printf("CENTER rgba=%u,%u,%u,%u err=0x%x\n", pix[0],pix[1],pix[2],pix[3], e);
    } else {
        printf("CENTER readback skipped (no surface)\n");
        fflush(stdout);
    }
    if(swp && s) { swp(d,s); }
    printf("GLSMOOTH_DONE\n");
    fflush(stdout);
    return 0;
}