#ifndef MMAL_CHAIN_PLAYER_MMAL_PLAYER_PIPELINE_H
#define MMAL_CHAIN_PLAYER_MMAL_PLAYER_PIPELINE_H

#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_connection.h"

struct mmal_player_pipeline;

// MMAL_TRUE: continue, MMAL_FALSE: shutdown pipeline
typedef MMAL_BOOL_T (*pipeline_eos_callback)(struct mmal_player_pipeline*, void*);
typedef void (*pipeline_exit_callback)(struct mmal_player_pipeline*, void*);

enum mmal_player_exit_reason {
    mmal_player_UNDEFINED = 0,
    mmal_player_TERMINATED,
    mmal_player_EOS,
    mmal_player_ERROR
};

struct mmal_player_pipeline
{
    MMAL_COMPONENT_T* container_reader;
    MMAL_COMPONENT_T* video_decoder;
    MMAL_COMPONENT_T* scheduler;
    MMAL_COMPONENT_T* video_renderer;

    MMAL_CONNECTION_T* reader_to_decoder;
    MMAL_CONNECTION_T* decoder_to_scheduler;
    MMAL_CONNECTION_T* scheduler_to_renderer;

    VCOS_SEMAPHORE_T sem_ready;
    MMAL_STATUS_T pipeline_status;
    MMAL_BOOL_T eos;

    VCOS_THREAD_T main_loop_thread;

    MMAL_BOOL_T after_seek;

    int rotation;
    int layer;

    char* uri;

    MMAL_BOOL_T terminate;

    int exit_reason;
    pipeline_eos_callback eos_callback;
    pipeline_exit_callback exit_callback;
    void* userdata;     // shared with eos_callback and exit_callback
};

MMAL_STATUS_T mmal_player_set_new_uri(struct mmal_player_pipeline* ctx, const char* next_uri);

MMAL_STATUS_T mmal_player_init(struct mmal_player_pipeline* ctx, const char* uri, int rotation, int layer);
MMAL_STATUS_T mmal_player_set_eos_callback(struct mmal_player_pipeline* ctx, pipeline_eos_callback cb, void* user);
MMAL_STATUS_T mmal_player_set_exit_callback(struct mmal_player_pipeline* ctx, pipeline_exit_callback cb, void* user);

MMAL_STATUS_T mmal_player_start(struct mmal_player_pipeline* ctx);
void mmal_player_stop(struct mmal_player_pipeline* ctx);
void mmal_player_join(struct mmal_player_pipeline* ctx);

void mmal_player_destroy(struct mmal_player_pipeline* ctx);

#endif //MMAL_CHAIN_PLAYER_MMAL_PLAYER_PIPELINE_H
