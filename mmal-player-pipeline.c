#include "mmal-player-pipeline.h"

#include <stdio.h>

#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"

#define CHECK_STATUS(status, msg) if (status != MMAL_SUCCESS) { fprintf(stderr, msg"\n"); goto error; }

static void control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    struct mmal_player_pipeline* ctx = (struct mmal_player_pipeline *) port->userdata;

    switch(buffer->cmd)
    {
        case MMAL_EVENT_ERROR:
            ctx->pipeline_status = (*(MMAL_STATUS_T *) buffer->data);
            break;
        case MMAL_EVENT_EOS:
            ctx->eos = MMAL_TRUE;
            break;
// not happen if TUNNELLED connection is set
        case MMAL_EVENT_FORMAT_CHANGED:
            fprintf(stderr, "%s: format changed event\n", port->name);
            mmal_connection_event_format_changed(ctx->reader_to_decoder, buffer);
            mmal_connection_event_format_changed(ctx->scheduler_to_renderer, buffer);
            mmal_connection_event_format_changed(ctx->decoder_to_scheduler, buffer);
            break;
    }

    mmal_buffer_header_release(buffer);

    /* The processing is done in our main thread */
    vcos_semaphore_post(&ctx->sem_ready);
}

static void connection_callback(MMAL_CONNECTION_T *connection)
{
    struct mmal_player_pipeline* ctx = (struct mmal_player_pipeline*) connection->user_data;

    /* The processing is done in our main thread */
    vcos_semaphore_post(&ctx->sem_ready);
}

MMAL_STATUS_T build_connections(struct mmal_player_pipeline* ctx)
{
    MMAL_STATUS_T status = MMAL_SUCCESS;

    if(ctx->reader_to_decoder == NULL) {
        status = mmal_connection_create(&ctx->reader_to_decoder, ctx->container_reader->output[0], ctx->video_decoder->input[0], 0);
        ctx->reader_to_decoder->callback = connection_callback;
        ctx->reader_to_decoder->user_data = ctx;
    }

    if(ctx->decoder_to_scheduler == NULL) {
        status = mmal_connection_create(&ctx->decoder_to_scheduler, ctx->video_decoder->output[0], ctx->scheduler->input[0], MMAL_CONNECTION_FLAG_TUNNELLING);
        ctx->decoder_to_scheduler->callback = connection_callback;
        ctx->decoder_to_scheduler->user_data = ctx;
    }

    if(ctx->scheduler_to_renderer == NULL) {
        status = mmal_connection_create(&ctx->scheduler_to_renderer, ctx->scheduler->output[0], ctx->video_renderer->input[0], MMAL_CONNECTION_FLAG_TUNNELLING);
        ctx->scheduler_to_renderer->callback = connection_callback;
        ctx->scheduler_to_renderer->user_data = ctx;
    }

    return status;
}

MMAL_STATUS_T setup_display_port(struct mmal_player_pipeline* ctx)
{
    MMAL_DISPLAYREGION_T display_region;

    memset(&display_region, 0, sizeof(MMAL_DISPLAYREGION_T));

    display_region.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
    display_region.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

    display_region.set |= MMAL_DISPLAY_SET_FULLSCREEN;
    display_region.fullscreen = MMAL_TRUE;

    display_region.set |= MMAL_DISPLAY_SET_ALPHA;
    display_region.alpha = MMAL_DISPLAY_ALPHA_FLAGS_DISCARD_LOWER_LAYERS;

    display_region.set |= MMAL_DISPLAY_SET_MODE;
    display_region.mode = MMAL_DISPLAY_MODE_LETTERBOX;

    display_region.set |= MMAL_DISPLAY_SET_LAYER;
    display_region.layer = ctx->layer;

    display_region.set |= MMAL_DISPLAY_SET_TRANSFORM;
    switch(ctx->rotation % 360)
    {
        case 0:display_region.transform = MMAL_DISPLAY_ROT0;
            break;
        case 90:display_region.transform = MMAL_DISPLAY_ROT90;
            break;
        case 180:display_region.transform = MMAL_DISPLAY_ROT180;
            break;
        case 270:display_region.transform = MMAL_DISPLAY_ROT270;
            break;
        default:
            // do nothing
            display_region.transform = MMAL_DISPLAY_ROT0;
            break;
    }

    return mmal_port_parameter_set(ctx->video_renderer->input[0], &display_region.hdr);
}

MMAL_STATUS_T set_callback_and_enable(struct mmal_player_pipeline* ctx, MMAL_COMPONENT_T* cmp)
{
    MMAL_STATUS_T status;

    cmp->control->userdata = (struct MMAL_PORT_USERDATA_T*)ctx;

    status = mmal_port_enable(cmp->control, control_callback);
    if(status != MMAL_SUCCESS) {
        fprintf(stderr, "failed to enable control port\n");
        return status;
    }
    status = mmal_component_enable(cmp);
    if(status != MMAL_SUCCESS) {
        fprintf(stderr, "failed to enable component\n");
        return status;
    }
    return status;
};

MMAL_STATUS_T build_components(struct mmal_player_pipeline* ctx, const char *next_uri)
{
    MMAL_STATUS_T status = MMAL_SUCCESS;

    ctx->after_seek = MMAL_TRUE;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CONTAINER_READER, &ctx->container_reader);
    CHECK_STATUS(status, "Unable to create container reader component");

    status = set_callback_and_enable(ctx, ctx->container_reader);
    CHECK_STATUS(status, "Unable to configure container reader component");

    if(ctx->uri != NULL) {
        free(ctx->uri);
    }
    ctx->uri = strdup(next_uri);
    status = mmal_util_port_set_uri(ctx->container_reader->control, next_uri);
    CHECK_STATUS(status, "Unable to set URI");

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_DECODER, &ctx->video_decoder);
    CHECK_STATUS(status, "Unable to create video decoder component");

    status = set_callback_and_enable(ctx, ctx->video_decoder);
    CHECK_STATUS(status, "Unable to configure video decoder component");

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_SCHEDULER, &ctx->scheduler);
    CHECK_STATUS(status, "Unable to create scheduler component");
    status = set_callback_and_enable(ctx, ctx->scheduler);
    CHECK_STATUS(status, "Unable to configure scheduler component");

    status = mmal_port_parameter_set_boolean(ctx->scheduler->clock[0], MMAL_PARAMETER_CLOCK_REFERENCE, MMAL_TRUE);
    CHECK_STATUS(status, "Unable to set clock reference");

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &ctx->video_renderer);
    CHECK_STATUS(status, "Unable to create video renderer component");
    status = set_callback_and_enable(ctx, ctx->video_renderer);
    CHECK_STATUS(status, "Unable to configure video renderer component");

    status = setup_display_port(ctx);
    CHECK_STATUS(status, "Unable to configure video renderer display configuration");

    status = build_connections(ctx);
    CHECK_STATUS(status, "Unable to establish connections");

    status = mmal_connection_enable(ctx->reader_to_decoder);
    CHECK_STATUS(status, "Unable to enable connection reader -> decoder");

    status = mmal_connection_enable(ctx->decoder_to_scheduler);
    CHECK_STATUS(status, "Unable to enable connection decoder -> scheduler");

    status = mmal_connection_enable(ctx->scheduler_to_renderer);
    CHECK_STATUS(status, "Unable to enable connection scheduler -> renderer");

error:
    return status;
}

#define LOG_IF_FAILS(status, format, ...) { if(status != MMAL_SUCCESS) fprintf(stderr, ("%s:%s(%d): " format "\n"), __FILE__, __func__, __LINE__, ##__VA_ARGS__); }

MMAL_STATUS_T mmal_container_seek(MMAL_COMPONENT_T* container_reader, int64_t offset, uint32_t flags)
{
    MMAL_PARAMETER_SEEK_T param;
    param.hdr.id = MMAL_PARAMETER_SEEK;
    param.hdr.size = sizeof(MMAL_PARAMETER_SEEK_T);
    param.flags = flags;
    param.offset = offset;

    return mmal_port_parameter_set(container_reader->control, &param.hdr);
}

MMAL_STATUS_T mmal_player_set_new_uri(struct mmal_player_pipeline* ctx, const char* next_uri)
{
    MMAL_STATUS_T status = MMAL_SUCCESS;

    mmal_port_parameter_set_boolean(ctx->scheduler->clock[0], MMAL_PARAMETER_CLOCK_ACTIVE, MMAL_FALSE);

#ifdef SEAMLESS_LOOP                    // unstable
    if(strcmp(ctx->uri, next_uri) == 0)
    {
        mmal_connection_disable(ctx->reader_to_decoder); mmal_connection_destroy(ctx->reader_to_decoder);
        ctx->reader_to_decoder = NULL;
        {
            // disable components
            mmal_component_disable(ctx->video_renderer);
            mmal_component_disable(ctx->scheduler);
            mmal_component_disable(ctx->video_decoder);
            mmal_component_disable(ctx->container_reader);

            mmal_container_seek(ctx->container_reader, 0, MMAL_PARAM_SEEK_FLAG_FORWARD);
            ctx->after_seek = MMAL_TRUE;

            build_connections(ctx);

            mmal_component_enable(ctx->container_reader);
            mmal_component_enable(ctx->scheduler);
            mmal_component_enable(ctx->video_decoder);
            mmal_component_enable(ctx->video_renderer);
        }
        mmal_connection_enable(ctx->reader_to_decoder);

        ctx->eos = MMAL_FALSE;

        mmal_port_parameter_set_boolean(ctx->scheduler->clock[0], MMAL_PARAMETER_CLOCK_ACTIVE, MMAL_TRUE);
    }
    else
#endif
    {
        // change movie
        mmal_connection_disable(ctx->scheduler_to_renderer); mmal_connection_destroy(ctx->scheduler_to_renderer);
        ctx->scheduler_to_renderer= NULL;

        mmal_connection_disable(ctx->decoder_to_scheduler); mmal_connection_destroy(ctx->decoder_to_scheduler);
        ctx->decoder_to_scheduler= NULL;

        mmal_connection_disable(ctx->reader_to_decoder); mmal_connection_destroy(ctx->reader_to_decoder);
        ctx->reader_to_decoder= NULL;

        // disable components
        mmal_component_disable(ctx->video_renderer); mmal_component_destroy(ctx->video_renderer);
        ctx->video_renderer= NULL;

        mmal_component_disable(ctx->scheduler); mmal_component_destroy(ctx->scheduler);
        ctx->scheduler= NULL;

        mmal_component_disable(ctx->video_decoder); mmal_component_destroy(ctx->video_decoder);
        ctx->video_decoder= NULL;

        mmal_component_disable(ctx->container_reader); mmal_component_destroy(ctx->container_reader);
        ctx->container_reader= NULL;

        // recreate components
        status = build_components(ctx, next_uri);

        mmal_component_enable(ctx->container_reader);
        mmal_component_enable(ctx->scheduler);
        mmal_component_enable(ctx->video_decoder);
        mmal_component_enable(ctx->video_renderer);

        mmal_connection_enable(ctx->reader_to_decoder);
        mmal_connection_enable(ctx->decoder_to_scheduler);
        mmal_connection_enable(ctx->scheduler_to_renderer);

        ctx->eos = MMAL_FALSE;

        mmal_port_parameter_set_boolean(ctx->scheduler->clock[0], MMAL_PARAMETER_CLOCK_ACTIVE, MMAL_TRUE);
    }
    return status;
}

MMAL_STATUS_T conn_pump(struct mmal_player_pipeline* ctx, MMAL_CONNECTION_T* connection)
{
    MMAL_BUFFER_HEADER_T *buffer;
    MMAL_STATUS_T status = MMAL_SUCCESS;

    if((connection->flags) & MMAL_CONNECTION_FLAG_TUNNELLING)
        return MMAL_SUCCESS; /* Nothing else to do in tunnelling mode */

    /* Send empty buffers to the output port of the connection */
    while((buffer = mmal_queue_get(connection->pool->queue)) != NULL) {
        status = mmal_port_send_buffer(connection->out, buffer);
        if(status != MMAL_SUCCESS) {
            fprintf(stderr, "failed to send buffer\n");
            return status;
        }
    }

    /* Send any queued buffer to the next component */
    while((buffer = mmal_queue_get(connection->queue)) != NULL) {
        status = mmal_port_send_buffer(connection->in, buffer);
        if(status != MMAL_SUCCESS) {
            fprintf(stderr, "failed to send buffer\n");
            return status;
        }
    }
    return status;
}

MMAL_STATUS_T conn_pump_for_container_reader(struct mmal_player_pipeline* ctx, MMAL_CONNECTION_T* connection)
{
    MMAL_BUFFER_HEADER_T *buffer;
    MMAL_STATUS_T status = MMAL_SUCCESS;

    if((connection->flags) & MMAL_CONNECTION_FLAG_TUNNELLING)
        return MMAL_SUCCESS; /* Nothing else to do in tunnelling mode */

    /* Send empty buffers to the output port of the connection */
    while((buffer = mmal_queue_get(connection->pool->queue)) != NULL) {
        status = mmal_port_send_buffer(connection->out, buffer);
        if(status != MMAL_SUCCESS) {
            fprintf(stderr, "failed to send buffer\n");
            return status;
        }
    }

    /* Send any queued buffer to the next component */
    while((buffer = mmal_queue_get(connection->queue)) != NULL) {
        if(ctx->after_seek) {
            fprintf(stderr, "set first-after-seek flag\n");
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_CONFIG;
            ctx->after_seek = MMAL_FALSE;
        }

        status = mmal_port_send_buffer(connection->in, buffer);
        if(status != MMAL_SUCCESS) {
            fprintf(stderr, "failed to send buffer\n");
            return status;
        }
    }
    return status;
}

void* mmal_player_pipeline_main_thread(void* user)
{
    struct mmal_player_pipeline* ctx = user;
    MMAL_STATUS_T status = MMAL_SUCCESS;

    ctx->exit_reason = mmal_player_UNDEFINED;

    while(1)
    {
//        fprintf(stderr, "waiting for semaphore to signal...");
        vcos_semaphore_wait(&ctx->sem_ready);
//        fprintf(stderr, "woken up by semaphore\n");

        if(ctx->terminate)
            break;

        /* Check for errors */
        status = ctx->pipeline_status;
        if(status != MMAL_SUCCESS) {
            break;
        }

        if(ctx->eos == MMAL_TRUE) {
            if(ctx->eos_callback && ctx->eos_callback(ctx, ctx->userdata))
                continue;
            break;
        }

        if((status = conn_pump_for_container_reader(ctx, ctx->reader_to_decoder)) != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to pump pipes in reader -> decoder: %d\n", status);
            break;
        }
        if((status = conn_pump(ctx, ctx->decoder_to_scheduler)) != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to pump pipes in decoder -> shceduler: %d\n", status);
            break;
        }
        if((status = conn_pump(ctx, ctx->scheduler_to_renderer)) != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to pump pipes in scheduler -> renderer: %d\n", status);
            break;
        }
    }

error:

    if(ctx->terminate)
        ctx->exit_reason = mmal_player_TERMINATED;
    else if(ctx->eos)
        ctx->exit_reason = mmal_player_EOS;
    else
        ctx->exit_reason = mmal_player_ERROR;

    if(ctx->exit_callback)
        ctx->exit_callback(ctx, ctx->userdata);

    status = mmal_connection_disable(ctx->reader_to_decoder);
    status = mmal_connection_disable(ctx->decoder_to_scheduler);
    status = mmal_connection_disable(ctx->scheduler_to_renderer);

    mmal_component_disable(ctx->video_renderer);
    mmal_component_disable(ctx->scheduler);
    mmal_component_disable(ctx->video_decoder);
    mmal_component_disable(ctx->container_reader);

    return NULL;
}

MMAL_STATUS_T mmal_player_init(struct mmal_player_pipeline* ctx, const char* uri)
{
    MMAL_STATUS_T status;

    memset(ctx, 0, sizeof(struct mmal_player_pipeline));

    ctx->layer = 128;

    vcos_semaphore_create(&ctx->sem_ready, "mmal_player:ready", 1);

    return build_components(ctx, uri);
}

MMAL_STATUS_T mmal_player_set_eos_callback(struct mmal_player_pipeline* ctx, pipeline_eos_callback cb, void* user)
{
    if(ctx == NULL)
        return EINVAL;

    ctx->eos_callback = cb;
    ctx->userdata = user;

    return MMAL_SUCCESS;
}

MMAL_STATUS_T mmal_player_set_exit_callback(struct mmal_player_pipeline* ctx, pipeline_exit_callback cb, void* user)
{
    if(ctx == NULL)
        return EINVAL;

    ctx->exit_callback = cb;
    ctx->userdata = user;

    return MMAL_SUCCESS;
}

MMAL_STATUS_T mmal_player_start(struct mmal_player_pipeline* ctx)
{
    VCOS_STATUS_T status;
    mmal_port_parameter_set_boolean(ctx->scheduler->clock[0], MMAL_PARAMETER_CLOCK_ACTIVE, MMAL_TRUE);

    ctx->exit_reason = mmal_player_UNDEFINED;

    status = vcos_thread_create(&ctx->main_loop_thread, "mmal-player:player thread", NULL, mmal_player_pipeline_main_thread, ctx);

    return status == VCOS_SUCCESS ? MMAL_SUCCESS : status;
}

void mmal_player_stop(struct mmal_player_pipeline* ctx)
{
    ctx->terminate = MMAL_TRUE;
    vcos_semaphore_post(&ctx->sem_ready);
}

void mmal_player_join(struct mmal_player_pipeline* ctx)
{
    void* ret = NULL;
    vcos_thread_join(&ctx->main_loop_thread, &ret);
}

void mmal_player_destroy(struct mmal_player_pipeline* ctx)
{
    if(ctx->scheduler_to_renderer != NULL)
        mmal_connection_destroy(ctx->scheduler_to_renderer);
    ctx->scheduler_to_renderer= NULL;

    if(ctx->decoder_to_scheduler != NULL)
        mmal_connection_destroy(ctx->decoder_to_scheduler);
    ctx->decoder_to_scheduler= NULL;

    if(ctx->reader_to_decoder != NULL)
        mmal_connection_destroy(ctx->reader_to_decoder);
    ctx->reader_to_decoder= NULL;

    // disable components
    if(ctx->video_renderer != NULL)
        mmal_component_destroy(ctx->video_renderer);
    ctx->video_renderer= NULL;

    if(ctx->scheduler != NULL)
        mmal_component_destroy(ctx->scheduler);
    ctx->scheduler= NULL;

    if(ctx->video_decoder != NULL)
        mmal_component_destroy(ctx->video_decoder);
    ctx->video_decoder= NULL;

    if(ctx->container_reader != NULL)
        mmal_component_destroy(ctx->container_reader);
    ctx->container_reader= NULL;

    if(ctx->uri != NULL) {
        free(ctx->uri);
        ctx->uri = NULL;
    }
}