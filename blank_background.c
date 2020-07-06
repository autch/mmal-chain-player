#include "blank_background.h"
#include <string.h>
#include <stdio.h>

#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_default_components.h"

static void callback_vr_input(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    mmal_buffer_header_release(buffer);
}

int blank_background_start(struct blank_background *context, int layer, int width, int height)
{
    if (context == NULL)
        return -1;

    memset(context, 0, sizeof(struct blank_background));

    context->layer = layer;
    context->screen_width = width;
    context->screen_height = height;

    mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &context->video_render);

    MMAL_PORT_T* input = context->video_render->input[0];

    input->format->encoding = MMAL_ENCODING_RGB24;
    input->format->es->video.width  = VCOS_ALIGN_UP(context->screen_width,  32);
    input->format->es->video.height = VCOS_ALIGN_UP(context->screen_height, 16);
    input->format->es->video.crop.x = 0;
    input->format->es->video.crop.y = 0;
    input->format->es->video.crop.width  = context->screen_width;
    input->format->es->video.crop.height = context->screen_height;

    mmal_port_format_commit(input);
    mmal_component_enable(context->video_render);
    mmal_port_parameter_set_boolean(input, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);

    input->buffer_size = input->buffer_size_recommended;
    input->buffer_num = input->buffer_num_recommended;
    if (input->buffer_num < 2)
        input->buffer_num = 2;

    context->pool = mmal_port_pool_create(input, input->buffer_num, input->buffer_size);
    if (!context->pool) {
        fprintf(stderr, "Oops, ,pool alloc failed\n");
        return -1;
    }

    {
        MMAL_DISPLAYREGION_T param;
        param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
        param.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

        param.set = MMAL_DISPLAY_SET_LAYER;
        param.layer = layer;    //On top of most things

        param.set |= MMAL_DISPLAY_SET_ALPHA;
        param.alpha = 255;    //0 = transparent, 255 = opaque

        param.set |= (MMAL_DISPLAY_SET_DEST_RECT | MMAL_DISPLAY_SET_FULLSCREEN);
        param.fullscreen = 0;
        param.dest_rect.x = 0;
        param.dest_rect.y = 0;
        param.dest_rect.width = context->screen_width;
        param.dest_rect.height = context->screen_height;
        mmal_port_parameter_set(input, &param.hdr);
    }

    mmal_port_enable(input, callback_vr_input);

    MMAL_BUFFER_HEADER_T* buffer = mmal_queue_wait(context->pool->queue);

    // Write something into the buffer.
    memset(buffer->data, 0, buffer->alloc_size);

    buffer->length = buffer->alloc_size;
    mmal_port_send_buffer(input, buffer);

    return 0;
}

int blank_background_stop(struct blank_background *context)
{
    if(context->video_render != NULL) {
        mmal_port_disable(context->video_render->input[0]);
        if(context->pool != NULL) {
            mmal_port_pool_destroy(context->video_render->input[0], context->pool);
            context->pool = NULL;
        }
        mmal_component_disable(context->video_render);
        mmal_component_destroy(context->video_render);
        context->video_render = NULL;
    }

    return 0;
}
