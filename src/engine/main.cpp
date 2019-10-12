// main.cpp: initialisation & main loop

#include "engine.h"

extern void cleargamma();

void cleanup()
{
    recorder::stop();
    cleanupserver();
    SDL_ShowCursor(1);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    cleargamma();
    freeocta(worldroot);
    extern void clear_command(); clear_command();
    extern void clear_console(); clear_console();
    extern void clear_mdls();    clear_mdls();
    extern void clear_sound();   clear_sound();
    closelogfile();
    SDL_Quit();
}

void quit(int *i = NULL)                     // normal exit     //NEW added int i
{
    extern void writeinitcfg();
    mod::deinit(); //NEW
    writeinitcfg();
    writeservercfg();
    abortconnect();
    disconnect();
    localdisconnect();
    if(!i || *i == EXIT_SUCCESS) //NEW
        writecfg();
    cleanup();
    mod::plugin::unloadplugins(true); //NEW
    exit(i ? *i : EXIT_SUCCESS); //NEW  i ? *i
}

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        //defvformatstring(msg,s,s); //NEW commented
        //NEW
        char msg[2048];
        va_list vargs;
        va_start(vargs, s);
        vsnprintf(msg, sizeof(msg), s, vargs);
        va_end(vargs);
        //NEW END

        logoutf("%s", msg);

        if(errors <= 1) // avoid recursion
        {
            if(SDL_WasInit(SDL_INIT_VIDEO))
            {
                SDL_ShowCursor(1);
                SDL_WM_GrabInput(SDL_GRAB_OFF);
                cleargamma();
            }
            #ifdef WIN32
                MessageBox(NULL, msg, "Cube 2: Sauerbraten fatal error", MB_OK|MB_SYSTEMMODAL);
            #endif
            SDL_Quit();
        }
    }

    _exit(EXIT_FAILURE); //NEW exit -> _exit (do not run destructors)
}

SDL_Surface *screen = NULL;

int curtime = 0, lastmillis = 1, elapsedtime = 0, totalmillis = 1;
atomic<int> atotalmillis(1); //NEW

dynent *player = NULL;

int initing = NOT_INITING;

bool initwarning(const char *desc, int level, int type)
{
    if(initing < level) 
    {
        addchange(desc, type);
        return true;
    }
    return false;
}

#define SCR_MINW 320
#define SCR_MINH 200
#define SCR_MAXW 10000
#define SCR_MAXH 10000
#define SCR_DEFAULTW 1024
#define SCR_DEFAULTH 768
VARF(scr_w, SCR_MINW, -1, SCR_MAXW, initwarning("screen resolution"));
VARF(scr_h, SCR_MINH, -1, SCR_MAXH, initwarning("screen resolution"));
VARF(colorbits, 0, 0, 32, initwarning("color depth"));
VARF(depthbits, 0, 0, 32, initwarning("depth-buffer precision"));
VARF(stencilbits, 0, 0, 32, initwarning("stencil-buffer precision"));
VARF(fsaa, -1, -1, 16, initwarning("anti-aliasing"));
VARF(vsync, -1, -1, 1, initwarning("vertical sync"));

void writeinitcfg()
{
    stream *f = openutf8file("init.cfg", "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// modify settings in game\n");
    extern int fullscreen;
    f->printf("fullscreen %d\n", fullscreen);
    f->printf("scr_w %d\n", scr_w);
    f->printf("scr_h %d\n", scr_h);
    f->printf("colorbits %d\n", colorbits);
    f->printf("depthbits %d\n", depthbits);
    f->printf("stencilbits %d\n", stencilbits);
    f->printf("fsaa %d\n", fsaa);
    f->printf("vsync %d\n", vsync);
    extern int shaderprecision;
    f->printf("shaderprecision %d\n", shaderprecision);
    extern int soundchans, soundfreq, soundbufferlen;
    f->printf("soundchans %d\n", soundchans);
    f->printf("soundfreq %d\n", soundfreq);
    f->printf("soundbufferlen %d\n", soundbufferlen);
    delete f;
}

COMMAND(quit, "i"); //NEW i

static void getbackgroundres(int &w, int &h)
{
    float wk = 1, hk = 1;
    if(w < 1024) wk = 1024.0f/w;
    if(h < 768) hk = 768.0f/h;
    wk = hk = max(wk, hk);
    w = int(ceil(w*wk));
    h = int(ceil(h*hk));
}

string backgroundcaption = "";
Texture *backgroundmapshot = NULL;
string backgroundmapname = "";
char *backgroundmapinfo = NULL;

void restorebackground()
{
    if(renderedframe) return;
    renderbackground(backgroundcaption[0] ? backgroundcaption : NULL, backgroundmapshot, backgroundmapname[0] ? backgroundmapname : NULL, backgroundmapinfo, true);
}

void bgquad(float x, float y, float w, float h, float tx = 0, float ty = 0, float tw = 1, float th = 1)
{
    gle::begin(GL_TRIANGLE_STRIP);
    gle::attribf(x,   y);   gle::attribf(tx,      ty);
    gle::attribf(x+w, y);   gle::attribf(tx + tw, ty);
    gle::attribf(x,   y+h); gle::attribf(tx,      ty + th);
    gle::attribf(x+w, y+h); gle::attribf(tx + tw, ty + th);
    gle::end();
}

void renderbackground(const char *caption, Texture *mapshot, const char *mapname, const char *mapinfo, bool restore, bool force)
{
    if(!inbetweenframes && !force) return;

    stopsounds(); // stop sounds while loading
 
    int w = screen->w, h = screen->h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);


    static int lastupdate = -1, lastw = -1, lasth = -1;
    static float backgroundu = 0, backgroundv = 0, detailu = 0, detailv = 0;
    static int numdecals = 0;
    static struct decal { float x, y, size; int side; } decals[12];
    if((renderedframe && !mainmenu && lastupdate != lastmillis) || lastw != w || lasth != h)
    {
        lastupdate = lastmillis;
        lastw = w;
        lasth = h;

        backgroundu = rndscale(1);
        backgroundv = rndscale(1);
        detailu = rndscale(1);
        detailv = rndscale(1);
        numdecals = sizeof(decals)/sizeof(decals[0]);
        numdecals = numdecals/3 + rnd((numdecals*2)/3 + 1);
        float maxsize = min(w, h)/16.0f;
        loopi(numdecals)
        {
            decal d = { rndscale(w), rndscale(h), maxsize/2 + rndscale(maxsize/2), rnd(2) };
            decals[i] = d;
        }
    }
    else if(lastupdate != lastmillis) lastupdate = lastmillis;

    loopi(restore ? 1 : 3)
    {
        hudmatrix.ortho(0, w, h, 0, -1, 1);
        resethudmatrix();

        hudshader->set();
        gle::colorf(1, 1, 1);

        gle::defvertex(2);
        gle::deftexcoord0();

        settexture("data/background.png", 0);
        float bu = w*0.67f/256.0f + backgroundu, bv = h*0.67f/256.0f + backgroundv;
        bgquad(0, 0, w, h, 0, 0, bu, bv);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        settexture("data/background_detail.png", 0);
        float du = w*0.8f/512.0f + detailu, dv = h*0.8f/512.0f + detailv;
        bgquad(0, 0, w, h, 0, 0, du, dv);
        settexture("data/background_decal.png", 3);
        gle::begin(GL_QUADS);
        loopj(numdecals)
        {
            float hsz = decals[j].size, hx = clamp(decals[j].x, hsz, w-hsz), hy = clamp(decals[j].y, hsz, h-hsz), side = decals[j].side;
            gle::attribf(hx-hsz, hy-hsz); gle::attribf(side,   0);
            gle::attribf(hx+hsz, hy-hsz); gle::attribf(1-side, 0);
            gle::attribf(hx+hsz, hy+hsz); gle::attribf(1-side, 1);
            gle::attribf(hx-hsz, hy+hsz); gle::attribf(side,   1);
        }
        gle::end();
        float lh = 0.5f*min(w, h), lw = lh*2,
              lx = 0.5f*(w - lw), ly = 0.5f*(h*0.5f - lh);
        settexture((maxtexsize ? min(maxtexsize, hwtexsize) : hwtexsize) >= 1024 && (screen->w > 1280 || screen->h > 800) ? "data/logo_1024.png" : "data/logo.png", 3);
        bgquad(lx, ly, lw, lh);
        if(caption)
        {
            int tw = text_width(caption);
            float tsz = 0.04f*min(w, h)/FONTH,
                  tx = 0.5f*(w - tw*tsz), ty = h - 0.075f*1.5f*min(w, h) - 1.25f*FONTH*tsz;
            pushhudmatrix();
            hudmatrix.translate(tx, ty, 0);
            hudmatrix.scale(tsz, tsz, 1);
            flushhudmatrix();
            draw_text(caption, 0, 0);
            pophudmatrix();
        }
        if(mapshot || mapname)
        {
            int infowidth = 12*FONTH;
            float sz = 0.35f*min(w, h), msz = (0.75f*min(w, h) - sz)/(infowidth + FONTH), x = 0.5f*(w-sz), y = ly+lh - sz/15;
            if(mapinfo)
            {
                int mw, mh;
                text_bounds(mapinfo, mw, mh, infowidth);
                x -= 0.5f*(mw*msz + FONTH*msz);
            }
            if(mapshot && mapshot!=notexture)
            {
                glBindTexture(GL_TEXTURE_2D, mapshot->id);
                bgquad(x, y, sz, sz);
            }
            else
            {
                int qw, qh;
                text_bounds("?", qw, qh);
                float qsz = sz*0.5f/max(qw, qh);
                pushhudmatrix();
                hudmatrix.translate(x + 0.5f*(sz - qw*qsz), y + 0.5f*(sz - qh*qsz), 0);
                hudmatrix.scale(qsz, qsz, 1);
                flushhudmatrix();
                draw_text("?", 0, 0);
                pophudmatrix();
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }        
            settexture("data/mapshot_frame.png", 3);
            bgquad(x, y, sz, sz);
            if(mapname)
            {
                int tw = text_width(mapname);
                float tsz = sz/(8*FONTH),
                      tx = 0.9f*sz - tw*tsz, ty = 0.9f*sz - FONTH*tsz;
                if(tx < 0.1f*sz) { tsz = 0.1f*sz/tw; tx = 0.1f; }
                pushhudmatrix();
                hudmatrix.translate(x+tx, y+ty, 0);
                hudmatrix.scale(tsz, tsz, 1);
                flushhudmatrix();
                draw_text(mapname, 0, 0);
                pophudmatrix();
            }
            if(mapinfo)
            {
                pushhudmatrix();
                hudmatrix.translate(x+sz+FONTH*msz, y, 0);
                hudmatrix.scale(msz, msz, 1);
                flushhudmatrix();
                draw_text(mapinfo, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, -1, infowidth);
                pophudmatrix();
            }
        }
        glDisable(GL_BLEND);
        gle::disable();
        if(!restore) swapbuffers(false);
    }

    if(!restore)
    {
        renderedframe = false;
        copystring(backgroundcaption, caption ? caption : "");
        backgroundmapshot = mapshot;
        copystring(backgroundmapname, mapname ? mapname : "");
        if(mapinfo != backgroundmapinfo)
        {
            DELETEA(backgroundmapinfo);
            if(mapinfo) backgroundmapinfo = newstring(mapinfo);
        }
    }
}

float loadprogress = 0;

void renderprogress(float bar, const char *text, GLuint tex, bool background)   // also used during loading
{
    if(!inbetweenframes || drawtex) return;

    clientkeepalive();      // make sure our connection doesn't time out while loading maps etc.
    
    #ifdef __APPLE__
    interceptkey(SDLK_UNKNOWN); // keep the event queue awake to avoid 'beachball' cursor
    #endif

    extern int sdl_backingstore_bug;
    if(background || sdl_backingstore_bug > 0) restorebackground();

    int w = screen->w, h = screen->h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    hudmatrix.ortho(0, w, h, 0, -1, 1);
    resethudmatrix();

    hudshader->set();
    gle::colorf(1, 1, 1);

    gle::defvertex(2);
    gle::deftexcoord0();

    float fh = 0.075f*min(w, h), fw = fh*10,
          fx = renderedframe ? w - fw - fh/4 : 0.5f*(w - fw), 
          fy = renderedframe ? fh/4 : h - fh*1.5f,
          fu1 = 0/512.0f, fu2 = 511/512.0f,
          fv1 = 0/64.0f, fv2 = 52/64.0f;
    settexture("data/loading_frame.png", 3);
    bgquad(fx, fy, fw, fh, fu1, fv1, fu2-fu1, fv2-fv1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float bw = fw*(511 - 2*17)/511.0f, bh = fh*20/52.0f,
          bx = fx + fw*17/511.0f, by = fy + fh*16/52.0f,
          bv1 = 0/32.0f, bv2 = 20/32.0f,
          su1 = 0/32.0f, su2 = 7/32.0f, sw = fw*7/511.0f,
          eu1 = 23/32.0f, eu2 = 30/32.0f, ew = fw*7/511.0f,
          mw = bw - sw - ew,
          ex = bx+sw + max(mw*bar, fw*7/511.0f);
    if(bar > 0)
    {
        settexture("data/loading_bar.png", 3);
        gle::begin(GL_QUADS);
        gle::attribf(bx,    by);    gle::attribf(su1, bv1);
        gle::attribf(bx+sw, by);    gle::attribf(su2, bv1);
        gle::attribf(bx+sw, by+bh); gle::attribf(su2, bv2);
        gle::attribf(bx,    by+bh); gle::attribf(su1, bv2);

        gle::attribf(bx+sw, by);    gle::attribf(su2, bv1);
        gle::attribf(ex,    by);    gle::attribf(eu1, bv1);
        gle::attribf(ex,    by+bh); gle::attribf(eu1, bv2);
        gle::attribf(bx+sw, by+bh); gle::attribf(su2, bv2);

        gle::attribf(ex,    by);    gle::attribf(eu1, bv1);
        gle::attribf(ex+ew, by);    gle::attribf(eu2, bv1);
        gle::attribf(ex+ew, by+bh); gle::attribf(eu2, bv2);
        gle::attribf(ex,    by+bh); gle::attribf(eu1, bv2);
        gle::end();
    }

    if(text)
    {
        int tw = text_width(text);
        float tsz = bh*0.8f/FONTH;
        if(tw*tsz > mw) tsz = mw/tw;
        pushhudmatrix();
        hudmatrix.translate(bx+sw, by + (bh - FONTH*tsz)/2, 0);
        hudmatrix.scale(tsz, tsz, 1);
        flushhudmatrix();
        draw_text(text, 0, 0);
        pophudmatrix();
    }

    glDisable(GL_BLEND);

    if(tex)
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        float sz = 0.35f*min(w, h), x = 0.5f*(w-sz), y = 0.5f*min(w, h) - sz/15;
        bgquad(x, y, sz, sz);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        settexture("data/mapshot_frame.png", 3);
        bgquad(x, y, sz, sz);
        glDisable(GL_BLEND);
    }

    gle::disable();
    swapbuffers(false);
}

void keyrepeat(bool on)
{
    SDL_EnableKeyRepeat(on ? SDL_DEFAULT_REPEAT_DELAY : 0,
                             SDL_DEFAULT_REPEAT_INTERVAL);
}

bool grabinput = false, minimized = false;

void inputgrab(bool on)
{
#ifndef WIN32
    if(!(screen->flags & SDL_FULLSCREEN)) SDL_WM_GrabInput(SDL_GRAB_OFF);
    else
#endif
    SDL_WM_GrabInput(on ? SDL_GRAB_ON : SDL_GRAB_OFF);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

void setfullscreen(bool enable)
{
    if(!screen) return;
#if defined(WIN32) || defined(__APPLE__)
    initwarning(enable ? "fullscreen" : "windowed");
#else
    if(enable == !(screen->flags&SDL_FULLSCREEN))
    {
        SDL_WM_ToggleFullScreen(screen);
        inputgrab(grabinput);
    }
#endif
}

#ifdef _DEBUG
VARF(fullscreen, 0, 0, 1, setfullscreen(fullscreen!=0));
#else
VARF(fullscreen, 0, 1, 1, setfullscreen(fullscreen!=0));
#endif

void screenres(int *w, int *h)
{
#if !defined(WIN32) && !defined(__APPLE__)
    if(initing >= INIT_RESET)
    {
#endif
        scr_w = clamp(*w, SCR_MINW, SCR_MAXW);
        scr_h = clamp(*h, SCR_MINH, SCR_MAXH);
#if defined(WIN32) || defined(__APPLE__)
        initwarning("screen resolution");
#else
        return;
    }
    SDL_Surface *surf = SDL_SetVideoMode(clamp(*w, SCR_MINW, SCR_MAXW), clamp(*h, SCR_MINH, SCR_MAXH), 0, SDL_OPENGL|(screen->flags&SDL_FULLSCREEN ? SDL_FULLSCREEN : SDL_RESIZABLE));
    if(!surf) return;
    screen = surf;
    scr_w = screen->w;
    scr_h = screen->h;
    glViewport(0, 0, scr_w, scr_h);
#endif
}

COMMAND(screenres, "ii");

/*
 * Added a new function to replace SDL_SetGamma() because SDL_SetGamma() does not work
 * on linux, or at least not with ATi cards
 */
#ifdef __linux__
MODSVARP(monitor, "DFP1");

static const char *filtersysvar(char *p)
{
    char *x, *y = p;
    for(x = p; *p; p++)
    {
        if((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '1' && *p <= '9'))
            *x++ = *p;
    }
    *x = '\0';
    return y;
}

static int changegamma(float red, float green, float blue)
{
    defformatstring(cmd, "which xrandr >/dev/null && xrandr --output %s --gamma %f:%f:%f 2>/dev/null", filtersysvar(monitor), red, green, blue);
    if(system(cmd)) return SDL_SetGamma(red, green, blue);
    return 0;
}

#define SDL_SetGamma changegamma
#endif //__linux__

static int curgamma = 100;
VARFP(gamma, 30, 100, 300,
{
    if(gamma == curgamma) return;
    curgamma = gamma;
	float f = gamma/100.0f;
    if(SDL_SetGamma(f,f,f)==-1) conoutf(CON_ERROR, "Could not set gamma: %s", SDL_GetError());
});

void restoregamma()
{
    if(curgamma == 100) return;
    float f = curgamma/100.0f;
    SDL_SetGamma(1, 1, 1);
    SDL_SetGamma(f, f, f);
}

void cleargamma()
{
    if(curgamma != 100) SDL_SetGamma(1, 1, 1);
}

VAR(dbgmodes, 0, 0, 1);

int desktopw = 0, desktoph = 0;

void setupscreen(int &usedcolorbits, int &useddepthbits, int &usedfsaa)
{
    int flags = SDL_RESIZABLE;
    #if defined(WIN32) || defined(__APPLE__)
    flags = 0;
    #endif
    if(fullscreen) flags = SDL_FULLSCREEN;
    SDL_Rect **modes = SDL_ListModes(NULL, SDL_OPENGL|flags);
    if(modes && modes!=(SDL_Rect **)-1)
    {
        int widest = -1, best = -1;
        for(int i = 0; modes[i]; i++)
        {
            if(dbgmodes) conoutf(CON_DEBUG, "mode[%d]: %d x %d", i, modes[i]->w, modes[i]->h);
            if(widest < 0 || modes[i]->w > modes[widest]->w || (modes[i]->w == modes[widest]->w && modes[i]->h > modes[widest]->h)) 
                widest = i; 
        }
        if(scr_w < 0 || scr_h < 0)
        {
            int w = scr_w, h = scr_h, ratiow = desktopw, ratioh = desktoph;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            if(ratiow <= 0 || ratioh <= 0) { ratiow = modes[widest]->w; ratioh = modes[widest]->h; }
            for(int i = 0; modes[i]; i++) if(modes[i]->w*ratioh == modes[i]->h*ratiow)
            {
                if(w <= modes[i]->w && h <= modes[i]->h && (best < 0 || modes[i]->w < modes[best]->w))
                    best = i;
            }
        } 
        if(best < 0)
        {
            int w = scr_w, h = scr_h;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            else if(w < 0) w = (h*SCR_DEFAULTW)/SCR_DEFAULTH;
            else if(h < 0) h = (w*SCR_DEFAULTH)/SCR_DEFAULTW;
            for(int i = 0; modes[i]; i++)
            {
                if(w <= modes[i]->w && h <= modes[i]->h && (best < 0 || modes[i]->w < modes[best]->w || (modes[i]->w == modes[best]->w && modes[i]->h < modes[best]->h)))
                    best = i;
            }
        }
        if(flags&SDL_FULLSCREEN)
        {
            if(best >= 0) { scr_w = modes[best]->w; scr_h = modes[best]->h; }
            else if(desktopw > 0 && desktoph > 0) { scr_w = desktopw; scr_h = desktoph; }
            else if(widest >= 0) { scr_w = modes[widest]->w; scr_h = modes[widest]->h; } 
        }
        else if(best < 0)
        { 
            scr_w = min(scr_w >= 0 ? scr_w : (scr_h >= 0 ? (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH : SCR_DEFAULTW), (int)modes[widest]->w); 
            scr_h = min(scr_h >= 0 ? scr_h : (scr_w >= 0 ? (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW : SCR_DEFAULTH), (int)modes[widest]->h);
        }
        if(dbgmodes) conoutf(CON_DEBUG, "selected %d x %d", scr_w, scr_h);
    }
    if(scr_w < 0 && scr_h < 0) { scr_w = SCR_DEFAULTW; scr_h = SCR_DEFAULTH; }
    else if(scr_w < 0) scr_w = (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH;
    else if(scr_h < 0) scr_h = (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW;

    bool hasbpp = true;
    if(colorbits)
        hasbpp = SDL_VideoModeOK(scr_w, scr_h, colorbits, SDL_OPENGL|flags)==colorbits;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#if SDL_VERSION_ATLEAST(1, 2, 11)
    if(vsync>=0) SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, vsync);
#endif
    static int configs[] =
    {
        0x7, /* try everything */
        0x6, 0x5, 0x3, /* try disabling one at a time */
        0x4, 0x2, 0x1, /* try disabling two at a time */
        0 /* try disabling everything */
    };
    int config = 0;
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if(!depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    if(!fsaa)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
    loopi(sizeof(configs)/sizeof(configs[0]))
    {
        config = configs[i];
        if(!depthbits && config&1) continue;
        if(!stencilbits && config&2) continue;
        if(fsaa<=0 && config&4) continue;
        if(depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, config&1 ? depthbits : 16);
        if(stencilbits)
        {
            hasstencil = config&2 ? stencilbits : 0;
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, hasstencil);
        }
        else hasstencil = 0;
        if(fsaa>0)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config&4 ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config&4 ? fsaa : 0);
        }
        screen = SDL_SetVideoMode(scr_w, scr_h, hasbpp ? colorbits : 0, SDL_OPENGL|flags);
        if(screen) break;
    }
    if(!screen) fatal("Unable to create OpenGL screen: %s", SDL_GetError());
    else
    {
        if(!hasbpp) conoutf(CON_WARN, "%d bit color buffer not supported - disabling", colorbits);
        if(depthbits && (config&1)==0) conoutf(CON_WARN, "%d bit z-buffer not supported - disabling", depthbits);
        if(stencilbits && (config&2)==0) conoutf(CON_WARN, "Stencil buffer not supported - disabling");
        if(fsaa>0 && (config&4)==0) conoutf(CON_WARN, "%dx anti-aliasing not supported - disabling", fsaa);
    }

    scr_w = screen->w;
    scr_h = screen->h;

    usedcolorbits = hasbpp ? colorbits : 0;
    useddepthbits = config&1 ? depthbits : 0;
    usedfsaa = config&4 ? fsaa : 0;
}

void resetgl()
{
    clearchanges(CHANGE_GFX);

    renderbackground("resetting OpenGL");

    extern void cleanupva();
    extern void cleanupparticles();
    extern void cleanupdecals();
    extern void cleanupblobs();
    extern void cleanupsky();
    extern void cleanupmodels();
    extern void cleanuptextures();
    extern void cleanuplightmaps();
    extern void cleanupblendmap();
    extern void cleanshadowmap();
    extern void cleanreflections();
    extern void cleanupglare();
    extern void cleanupdepthfx();
    extern void cleanupshaders();
    extern void cleanupgl();
    recorder::cleanup();
    cleanupva();
    cleanupparticles();
    cleanupblobs();
    cleanupsky();
    cleanupmodels();
    cleanuptextures();
    cleanuplightmaps();
    cleanupblendmap();
    cleanshadowmap();
    cleanreflections();
    cleanupglare();
    cleanupdepthfx();
    cleanupshaders();
    cleanupgl();
    
    SDL_SetVideoMode(0, 0, 0, 0);

    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);

    extern void reloadfonts();
    extern void reloadtextures();
    extern void reloadshaders();
    inbetweenframes = false;
    if(!reloadtexture(*notexture) ||
       !reloadtexture("data/logo.png") ||
       !reloadtexture("data/logo_1024.png") || 
       !reloadtexture("data/background.png") ||
       !reloadtexture("data/background_detail.png") ||
       !reloadtexture("data/background_decal.png") ||
       !reloadtexture("data/mapshot_frame.png") ||
       !reloadtexture("data/loading_frame.png") ||
       !reloadtexture("data/loading_bar.png"))
        fatal("failed to reload core texture");
    reloadfonts();
    inbetweenframes = true;
    renderbackground("initializing...");
	restoregamma();
    reloadshaders();
    reloadtextures();
    initlights();
    allchanged(true);
}

COMMAND(resetgl, "");

vector<SDL_Event> events;

void pushevent(const SDL_Event &e)
{
    events.add(e); 
}

static bool filterevent(const SDL_Event &event)
{
    switch(event.type)
    {
        case SDL_MOUSEMOTION:
            #ifndef WIN32
            if(grabinput && !(screen->flags&SDL_FULLSCREEN))
            {
                if(event.motion.x == screen->w / 2 && event.motion.y == screen->h / 2) 
                    return false;  // ignore any motion events generated by SDL_WarpMouse
                #ifdef __APPLE__
                if(event.motion.y == 0) 
                    return false;  // let mac users drag windows via the title bar
                #endif
            }
            #endif
            break;
    }
    return true;
}

static inline bool pollevent(SDL_Event &event)
{
    while(SDL_PollEvent(&event))
    {
        if(filterevent(event)) return true;
    }
    return false;
}

bool interceptkey(int sym)
{
    static int lastintercept = SDLK_UNKNOWN;
    int len = lastintercept == sym ? events.length() : 0;
    SDL_Event event;
    while(pollevent(event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION: break;
            default: pushevent(event); break;
        }
    }
    lastintercept = sym;
    if(sym != SDLK_UNKNOWN) for(int i = len; i < events.length(); i++)
    {
        if(events[i].type == SDL_KEYDOWN && events[i].key.keysym.sym == sym) { events.remove(i); return true; }
    }
    return false;
}

static void ignoremousemotion()
{
    SDL_Event e;
    SDL_PumpEvents();
    while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_MOUSEMOTION)));
}

static void resetmousemotion()
{
#ifndef WIN32
    if(grabinput && !(screen->flags&SDL_FULLSCREEN))
    {
        SDL_WarpMouse(screen->w / 2, screen->h / 2);
    }
#endif
}

static void checkmousemotion(int &dx, int &dy)
{
    loopv(events)
    {
        SDL_Event &event = events[i];
        if(event.type != SDL_MOUSEMOTION)
        { 
            if(i > 0) events.remove(0, i); 
            return; 
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
    events.setsize(0);
    SDL_Event event;
    while(pollevent(event))
    {
        if(event.type != SDL_MOUSEMOTION)
        {
            events.add(event);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
}

void checkinput()
{
    SDL_Event event;
    int lasttype = 0, lastbut = 0;
    bool mousemoved = false; 
    while(events.length() || pollevent(event))
    {
        if(events.length()) event = events.remove(0);

        switch(event.type)
        {
            case SDL_QUIT:
                quit();
                return;

            #if !defined(WIN32) && !defined(__APPLE__)
            case SDL_VIDEORESIZE:
                screenres(&event.resize.w, &event.resize.h);
                break;
            #endif

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                keypress(event.key.keysym.sym, event.key.state==SDL_PRESSED, uni2cube(event.key.keysym.unicode));
                break;

            case SDL_ACTIVEEVENT:
                if(event.active.state & SDL_APPINPUTFOCUS)
                    inputgrab(grabinput = event.active.gain!=0);
                if(event.active.state & SDL_APPACTIVE)
                    minimized = !event.active.gain;
                break;

            case SDL_MOUSEMOTION:
                if(grabinput)
                {
                    int dx = event.motion.xrel, dy = event.motion.yrel;
                    checkmousemotion(dx, dy);
                    if(!g3d_movecursor(dx, dy)) mousemove(dx, dy);
                    mousemoved = true;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if(lasttype==event.type && lastbut==event.button.button) break; // why?? get event twice without it
                keypress(-event.button.button, event.button.state!=0, 0);
                lasttype = event.type;
                lastbut = event.button.button;
                break;
        }
    }
    if(mousemoved) resetmousemotion();
}

void swapbuffers(bool overlay)
{
    recorder::capture(overlay);
    SDL_GL_SwapBuffers();
}
 
//NEW
static void maxfpschanged()
{
    extern int maxfps;
    if(!maxfps) ++maxfps;
    else if(maxfps < 0)
    {
        static const char *vendor = (const char *)glGetString(GL_VENDOR);
        if(!strstr(vendor, "NVIDIA"))
        {
            mod::erroroutf_r("'/maxfps -1' is limited to nvidia graphic cards for now");
            conoutf("setting maxfps to 1000 instead");
            maxfps = 1000;
        }
        else
        {
            conoutf("PLEASE MONITOR THE TEMPERATURE OF YOUR HARDWARE WHILE USING '/maxfps -1'");
            conoutf("I AM NOT LIABLE FOR ANY HARDWARE DAMAGES!");
        }
    }
}
//NEW END
 
VAR(menufps, 0, 60, 1000);
VARFP(maxfps, -1, 200, 1000, maxfpschanged()); //NEW VARFP instead of VARP   -1 as MINVAL to disable the fps limiter

void limitfps(int &millis, int curmillis)
{
    if(maxfps < 0 && !mainmenu && !minimized) return; //NEW
    int limit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(!limit) return;
    static int fpserror = 0;
    int delay = 1000/limit - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%limit;
        if(fpserror >= limit)
        {
            ++delay;
            fpserror -= limit;
        }
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        }
    }
}


#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
//NEW
#ifdef PUBLIC_RELEASE
bool createminidump(EXCEPTION_POINTERS *ep, const char *dmpfile)
{
    bool res = false;

    HANDLE file = CreateFile(dmpfile, GENERIC_READ|GENERIC_WRITE,
                             0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if(file && file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei;

        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = ep;
        mdei.ClientPointers = FALSE;

        MINIDUMP_TYPE mdt = MiniDumpNormal;

        res = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                file, mdt, ep? &mdei : NULL, NULL, NULL) == TRUE;
        CloseHandle(file);
    }

    return res;
}
#endif
//NEW END
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
//NEW
#ifdef PUBLIC_RELEASE
    string buf;
    mod::strtool home(buf, sizeof(buf));
    home = gethomedir();
    if(home) home += "/";
    defformatstring(dmp)("%scrash.v%s-%s.%d.dmp", home.str(), CLIENTVERSION.str().str(),
                         getwcrevision(), totalmillis);
    path(dmp);

    if (!createminidump(ep, dmp))
        copystring(dmp, "failed to create dump");

    const char *fmt =
        "The client has crashed. Please write me an email (t.poechtrager@gmail.com) "
        "including a full description of how it happened so we can fix this issue in "
        "future releases. Please attach the created minidump to the email "
        "(%s). Thanks!";

    fatal(fmt, dmp);
    return;
#endif
//NEW END
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out, "Cube 2: Sauerbraten Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#ifdef _AMD64_
	STACKFRAME64 sf = {{context->Rip, 0, AddrModeFlat}, {}, {context->Rbp, 0, AddrModeFlat}, {context->Rsp, 0, AddrModeFlat}, 0};
    while(::StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL64 sym; char symext[sizeof(IMAGEHLP_SYMBOL64) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(line);
        DWORD64 symoff;
		DWORD lineoff;
        if(SymGetSymFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#else
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL sym; char symext[sizeof(IMAGEHLP_SYMBOL) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE line;
		line.SizeOfStruct = sizeof(line);
        DWORD symoff, lineoff;
        if(SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#endif
        {
            char *del = strrchr(line.FileName, '\\');
            formatstring(t, "%s - %s [%d]\n", sym.Name, del ? del + 1 : line.FileName, line.LineNumber);
            concatstring(out, t);
        }
    }
    fatal(out);
}
#endif

#define MAXFPSHISTORY 60

int fpspos = 0, fpshistory[MAXFPSHISTORY];

void resetfpshistory()
{
    loopi(MAXFPSHISTORY) fpshistory[i] = 1;
    fpspos = 0;
}

static int rfps = 0; //NEW

void updatefpshistory(int millis)
{
    //NEW
    if(maxfps < 0)
    {
        static int last = 0, fps = 0;
        if(totalmillis-last >= 1000)
        {
            rfps = fps; fps = 0;
            last = totalmillis;
        }
        else ++fps;
        return;
    }
    //NEW END
    fpshistory[fpspos++] = max(1, min(1000, millis));
    if(fpspos>=MAXFPSHISTORY) fpspos = 0;
}

void getfps(int &fps, int &bestdiff, int &worstdiff)
{
    //NEW
    if(maxfps < 0)
    {
        fps = rfps;
        bestdiff = worstdiff = 0;
        if(fps) return;
    }
    //NEW END
    int total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    loopi(MAXFPSHISTORY-1)
    {
        int millis = fpshistory[i];
        total += millis;
        if(millis < best) best = millis;
        if(millis > worst) worst = millis;
    }

    fps = (1000*MAXFPSHISTORY)/total;
    bestdiff = 1000/best-fps;
    worstdiff = fps-1000/worst;
}

void getfps_(int *raw)
{
    int fps, bestdiff, worstdiff;
    if(*raw) fps = 1000/fpshistory[(fpspos+MAXFPSHISTORY-1)%MAXFPSHISTORY];
    else getfps(fps, bestdiff, worstdiff);
    intret(fps);
}

COMMANDN(getfps, getfps_, "i");

bool inbetweenframes = false, renderedframe = true;

static bool findarg(int argc, char **argv, const char *str)
{
    for(int i = 1; i<argc; i++) if(strstr(argv[i], str)==argv[i]) return true;
    return false;
}

static int clockrealbase = 0, clockvirtbase = 0;
static void clockreset() { clockrealbase = SDL_GetTicks(); clockvirtbase = totalmillis; }
VARFP(clockerror, 990000, 1000000, 1010000, clockreset());
VARFP(clockfix, 0, 0, 1, clockreset());

int getclockmillis()
{
    int millis = SDL_GetTicks() - clockrealbase;
    if(clockfix) millis = int(millis*(double(clockerror)/1000000));
    millis += clockvirtbase;
    return max(millis, totalmillis);
}

VAR(numcpus, 1, 1, 16);

//NEW
MODVARP(benchmarkframe, 0, 0, 1);
static double totaltime = 0.0;
static uint totalcounter = 0;

#ifdef __GNUC__
__inline 
#endif
void framebenchmark(bool create = false)
{
    if(!benchmarkframe) return;
    static benchmark bench("frame", true);
    if(create && !bench.active())
    {
        if(vsync) conoutf("benchmarking frames doesn't work properly with vsync enabled");
        else bench.start();
    }
    else
    {
        if(bench.active())
        {
            bench.calc();
            double totaltimeold = totaltime;
            totaltime += bench.gettime();
            if(totaltime<totaltimeold)
            {
                totaltime = 0.0;
                totalcounter = 0;
            }
            else
            {
                totalcounter++;
                conoutf("frame took %.6lf ms | median: %.6lf ms | possible fps: %.0f", 
                    bench.gettime(), totaltime/double(totalcounter ? totalcounter : 1), 1000.0/bench.gettime());
            }
            bench.reset();
        }
    }
}

ICOMMAND(resetframebenchmark, "", (), { totaltime = 0.0; totalcounter = 0.0; });
//NEW END

int main(int argc, char **argv)
{
    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    #ifndef __clang__ //NEW
    __try {
    #endif
    #endif
    #endif
    #endif

    setlogfile(NULL);

    int dedicated = 0;
    char *load = NULL, *initscript = NULL;

    initing = INIT_RESET;
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': 
			{
				const char *dir = sethomedir(&argv[i][2]);
				if(dir) logoutf("Using home directory: %s", dir);
				break;
			}
        }
    }
    execfile("init.cfg", false);
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': /* parsed first */ break;
            case 'r': /* compat, ignore */ break;
            case 'k':
            {
                const char *dir = addpackagedir(&argv[i][2]);
                if(dir) logoutf("Adding package directory: %s", dir);
                break;
            }
            case 'g': logoutf("Setting log file: %s", &argv[i][2]); setlogfile(&argv[i][2]); break;
            case 'd': dedicated = atoi(&argv[i][2]); if(dedicated<=0) dedicated = 2; break;
            case 'w': scr_w = clamp(atoi(&argv[i][2]), SCR_MINW, SCR_MAXW); if(!findarg(argc, argv, "-h")) scr_h = -1; break;
            case 'h': scr_h = clamp(atoi(&argv[i][2]), SCR_MINH, SCR_MAXH); if(!findarg(argc, argv, "-w")) scr_w = -1; break;
            case 'z': depthbits = atoi(&argv[i][2]); break;
            case 'b': colorbits = atoi(&argv[i][2]); break;
            case 'a': fsaa = atoi(&argv[i][2]); break;
            case 'v': vsync = atoi(&argv[i][2]); break;
            case 't': fullscreen = atoi(&argv[i][2]); break;
            case 's': stencilbits = atoi(&argv[i][2]); break;
            case 'f': 
            {
                extern int shaderprecision;
                int prec = shaderprecision;
                for(int j = 2; argv[i][j]; j++) switch(argv[i][j])
                {
                    case '0': prec = 0; break;
                    case '1': case '2': case '3': prec = argv[i][j] - '1'; break;
                    default: break;
                }
                shaderprecision = prec;
                break;
            }
            case 'l': 
            {
                char pkgdir[] = "packages/"; 
                load = strstr(path(&argv[i][2]), path(pkgdir)); 
                if(load) load += sizeof(pkgdir)-1; 
                else load = &argv[i][2]; 
                break;
            }
            case 'x': initscript = &argv[i][2]; break;
            default: if(!serveroption(argv[i])) gameargs.add(argv[i]); break;
        }
        else gameargs.add(argv[i]);
    }
    initing = NOT_INITING;

    numcpus = clamp(guessnumcpus(), 1, 16);

    if(dedicated <= 1)
    {
        logoutf("init: sdl");

        int par = 0;
        #ifdef _DEBUG
        par = SDL_INIT_NOPARACHUTE;
        #ifdef WIN32
        SetEnvironmentVariable("SDL_DEBUG", "1");
        #endif
        #endif

        if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|SDL_INIT_AUDIO|par)<0) fatal("Unable to initialize SDL: %s", SDL_GetError());
        
        const SDL_version *v = SDL_Linked_Version();
        conoutf(CON_INIT, "Library: SDL %u.%u.%u", v->major, v->minor, v->patch);
    }
    
    logoutf("init: net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);

    logoutf("init: game");
    game::parseoptions(gameargs);
    initserver(dedicated>0, dedicated>1);  // never returns if dedicated
    ASSERT(dedicated <= 1);
    game::initclient();

    logoutf("init: video");
    const SDL_VideoInfo *video = SDL_GetVideoInfo();
    if(video) 
    {
        desktopw = video->current_w;
        desktoph = video->current_h;
    }
    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);

    SDL_WM_SetCaption("Cube 2: Sauerbraten", NULL);
    keyrepeat(false);
    SDL_ShowCursor(0);

    logoutf("init: gl");
    gl_checkextensions();
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);
    notexture = textureload("packages/textures/notexture.png");
    if(!notexture) fatal("could not find core textures");

    logoutf("init: console");
    if(!execfile("data/stdlib.cfg", false)) fatal("cannot find data files (you are running from the wrong folder, try .bat file in the main folder)");   // this is the first file we load.
    if(!execfile("data/font.cfg", false)) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

    inbetweenframes = true;
    renderbackground("initializing...");

    logoutf("init: effects");
    loadshaders();
    particleinit();
    initdecals();

    logoutf("init: world");
    camera1 = player = game::iterdynents(0);
    emptymap(0, true, NULL, false);

    logoutf("init: sound");
    initsound();

    mod::init(); //NEW

    logoutf("init: cfg");
    execfile("data/keymap.cfg");
    execfile("data/stdedit.cfg");
    execfile("data/menus.cfg");
    execfile("data/sounds.cfg");
    execfile("data/heightmap.cfg");
    execfile("data/blendbrush.cfg");
    if(game::savedservers()) execfile(game::savedservers(), false);
    
    identflags |= IDF_PERSIST;
    
    initing = INIT_LOAD;
    execfile(game::wcconfig(), false); //NEW
    if(!execfile(game::savedconfig(), false)) 
    {
        execfile(game::defaultconfig());
        writecfg(game::restoreconfig());
    }
    mod::chat::setupbinds();           //NEW
    mod::wcautocheckversion();         //NEW
    execfile(game::autoexec(), false);
    loadhistory();                     //NEW
    mod::chat::init();                 //NEW
    initing = NOT_INITING;

    identflags &= ~IDF_PERSIST;

    string gamecfgname;
    copystring(gamecfgname, "data/game_");
    concatstring(gamecfgname, game::gameident());
    concatstring(gamecfgname, ".cfg");
    execfile(gamecfgname);
    
    game::loadconfigs();

    identflags |= IDF_PERSIST;

    if(execfile("once.cfg", false)) remove(findfile("once.cfg", "rb"));

    if(load)
    {
        logoutf("init: localconnect");
        //localconnect();
        game::changemap(load);
    }

    if(initscript) execute(initscript);

    logoutf("init: mainloop");

    //NEW
    // MSVC requires the following to be a function, otherwise the compiler
    // complains about destructible objects used in a __try block
    [](){ conoutf("WC NG version: %s", CLIENTVERSION.str().str()); }();
    //NEW END

    initmumble();
    resetfpshistory();

    inputgrab(grabinput = true);
    ignoremousemotion();

    execfile(game::wcautoexec(), false); //NEW
    mod::event::run(mod::event::STARTUP); //NEW

    for(;;)
    {
        static int frames = 0;
        int millis = getclockmillis();
        limitfps(millis, totalmillis);
        framebenchmark(true); //NEW
        elapsedtime = millis - totalmillis;
        static int timeerr = 0;
        int scaledtime = game::scaletime(elapsedtime) + timeerr;
        curtime = scaledtime/100;
        timeerr = scaledtime%100;
        if(!multiplayer(false) && curtime>200) curtime = 200;
        if(game::ispaused()) curtime = 0;
		lastmillis += curtime;
        totalmillis = millis;
        atotalmillis = millis; //NEW
        updatetime();
 
        checkinput();
        menuprocess();
        tryedit();

        if(lastmillis) game::updateworld();

        checksleep(lastmillis);

        serverslice(false, 0);

        if(frames) updatefpshistory(elapsedtime);
        frames++;

        // miscellaneous general game effects
        recomputecamera();
        updateparticles();
        updatesounds();
    
        mod::slice(); //NEW

        if(minimized && !recorder::isrecording()) //NEW !recorder::isrecording()
        {
            framebenchmark(); //NEW
            continue;
        }

        inbetweenframes = false;
        if(mainmenu) gl_drawmainmenu(screen->w, screen->h);
        else gl_drawframe(screen->w, screen->h);
        swapbuffers();
        renderedframe = inbetweenframes = true;
        framebenchmark(); //NEW
    }
    
    ASSERT(0);   
    return EXIT_FAILURE;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__) && !defined(__clang__) //NEW !defined(__clang__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}
