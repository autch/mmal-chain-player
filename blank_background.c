#include "blank_background.h"
#include <assert.h>
#include <string.h>

// code stolen from hello_videocube/triangle.c

static const EGLint attribute_list[] =
{
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
};

int blank_background_start(struct blank_background *context, int layer)
{
    int32_t success = 0;
    EGLBoolean result;
    EGLint num_config;

    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    EGLConfig config;

    if (context == NULL)
        return -1;

    memset(context, 0, sizeof(struct blank_background));

    context->layer = layer;

    context->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(context->display != EGL_NO_DISPLAY);

    // initialize the EGL display connection
    result = eglInitialize(context->display, NULL, NULL);
    assert(EGL_FALSE != result);

    // get an appropriate EGL frame buffer configuration
    result = eglChooseConfig(context->display, attribute_list, &config, 1, &num_config);
    assert(EGL_FALSE != result);

    // create an EGL rendering context
    context->context = eglCreateContext(context->display, config, EGL_NO_CONTEXT, NULL);
    assert(context->context != EGL_NO_CONTEXT);

    // create an EGL window surface
    success = graphics_get_display_size(0 /* LCD */, &context->screen_width, &context->screen_height);
    assert(success >= 0);

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = context->screen_width;
    dst_rect.height = context->screen_height;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = context->screen_width << 16;
    src_rect.height = context->screen_height << 16;

    context->dispman_display = vc_dispmanx_display_open(0 /* LCD */);
    dispman_update = vc_dispmanx_update_start(0);

    context->dispman_element = vc_dispmanx_element_add(dispman_update, context->dispman_display,
                                                       layer, &dst_rect, 0 /*src*/,
                                                       &src_rect, DISPMANX_PROTECTION_NONE,
                                                       NULL /*alpha*/, NULL /*clamp*/,
                                                       DISPMANX_NO_ROTATE /*transform*/);

    context->native_window.element = context->dispman_element;
    context->native_window.width = context->screen_width;
    context->native_window.height = context->screen_height;
    vc_dispmanx_update_submit_sync(dispman_update);

    context->surface = eglCreateWindowSurface(context->display, config, &context->native_window, NULL);
    assert(context->surface != EGL_NO_SURFACE);

    // connect the context to the surface
    result = eglMakeCurrent(context->display, context->surface, context->surface, context->context);
    assert(EGL_FALSE != result);

    // Set background color and clear buffers
    glClearColor(0.f, 0.f, 0.f, 1.f);

    // Enable back face culling.
    glEnable(GL_CULL_FACE);
    glMatrixMode(GL_MODELVIEW);
    glClear(GL_COLOR_BUFFER_BIT);

    eglSwapBuffers(context->display, context->surface);

    return 0;
}

int blank_background_stop(struct blank_background *context)
{
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    int s;

    if (context->display != NULL && context->surface != NULL) {
        // clear screen
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(context->display, context->surface);
    }

    if (context->surface != NULL) {
        eglDestroySurface(context->display, context->surface);
        context->surface = NULL;
    }

    if (context->dispman_element && context->dispman_display) {
        dispman_update = vc_dispmanx_update_start(0);
        s = vc_dispmanx_element_remove(dispman_update, context->dispman_element);
        assert(s == 0);
        vc_dispmanx_update_submit_sync(dispman_update);
        s = vc_dispmanx_display_close(context->dispman_display);
        assert (s == 0);
        context->dispman_display = 0;
        context->dispman_element = 0;
    }

    if (context->context != NULL && context->display != NULL) {
        // Release OpenGL resources
        eglMakeCurrent(context->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(context->display, context->context);
        eglTerminate(context->display);
        context->context = NULL;
        context->display = NULL;
    }

    return 0;
}
