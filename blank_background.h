#ifndef MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H
#define MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H

#include "bcm_host.h"

#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

struct blank_background
{
    int layer;

    uint32_t screen_width, screen_height;

    EGL_DISPMANX_WINDOW_T native_window;

    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_ELEMENT_HANDLE_T dispman_element;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
};

int blank_background_start(struct blank_background* context, int layer);
int blank_background_stop(struct blank_background* context);

#endif //MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H
