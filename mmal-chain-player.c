#include "bcm_host.h"
#include "interface/mmal/mmal.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>

#include "blank_background.h"
#include "mmal-player-pipeline.h"

struct player_context
{
    int rotation;
    int loop;               // 0: no loop, -1: infinity, 1~: repeat n times
    int current_iter;
    int loop_overall;

    int reason;             // why player thread has exit

    char **av;
    int ac, ai;

    struct blank_background bb;
    struct mmal_player_pipeline* player;
    struct mmal_player_pipeline* old_player;


    VCOS_SEMAPHORE_T sem_event;
};

#define CHECK_STATUS(status, msg) if (status != MMAL_SUCCESS) { fprintf(stderr, msg"\n"); goto error; }

static const struct option long_options[] =
{
    {"rotate",   required_argument, NULL, 'r'},
    {"loop",     optional_argument, NULL, 'l'},
    {"loop-all", no_argument,       NULL, 'L'},
    {NULL, 0,                       NULL, 0}
};

struct mmal_player_pipeline* make_player(struct player_context* ctx, char* uri);


MMAL_BOOL_T chain_player_eos_callback(struct mmal_player_pipeline* pipeline, void* user)
{
    struct player_context* ctx = user;
    char* next_uri;

    if((ctx->loop > 0 && --ctx->current_iter > 0) || ctx->loop == -1) {
        // continue with current mov
        next_uri = ctx->av[ctx->ai];
    } else {
        // proceed to next mov
        if(ctx->av[ctx->ai + 1] == NULL || ctx->ai + 1 > ctx->ac) {
            if(ctx->loop_overall) {
                ctx->ai = optind - 1;
                // fall thru
            } else {
                fprintf(stderr, "Exiting\n");
                return MMAL_FALSE;
            }
        }

        next_uri = ctx->av[++ctx->ai];
        if(ctx->loop > 0)
            ctx->current_iter = ctx->loop;
    }

    if(ctx->old_player != NULL) {
        mmal_player_join(ctx->old_player);
        mmal_player_destroy(ctx->old_player);
        ctx->old_player = NULL;
    }

    mmal_player_set_exit_callback(pipeline, NULL, ctx);
    mmal_player_set_eos_callback(pipeline, NULL, ctx);

    struct mmal_player_pipeline* new_player = make_player(ctx, next_uri);
    if(new_player == NULL) {
        fprintf(stderr, "unable to recreate player\n");
        return MMAL_FALSE;
    }

    ctx->player = new_player;
    ctx->old_player = pipeline;

    mmal_player_stop(pipeline);
    mmal_player_start(new_player);

    return MMAL_TRUE;
}

void chain_player_exit_callback(struct mmal_player_pipeline* pipeline, void* user)
{
    struct player_context* ctx = user;

    ctx->reason = pipeline->exit_reason;
    vcos_semaphore_post(&ctx->sem_event);
}

struct mmal_player_pipeline* make_player(struct player_context* ctx, char* uri)
{
    struct mmal_player_pipeline* player;
    MMAL_STATUS_T status;

    player = mmal_player_create(uri, ctx->rotation, 128);
    if(player == NULL) {
        return NULL;
    }

    mmal_player_set_eos_callback(player, chain_player_eos_callback, ctx);
    mmal_player_set_exit_callback(player, chain_player_exit_callback, ctx);

    return player;
}

int usage(int ac, char** av)
{
    printf("Usage: %s [-r DEGREE] [-l [TIMES]] [-L] FILES...\n", *av);
    printf("\t-r DEGREE\tRotate DEGREEs clockwise\n");
    printf("\t-l [TIMES]\tRepeat each file by TIMES, -1 indicates infinitely\n");
    printf("\t-L\t\tCycle files\n");
    printf("\tFILES\t\tAny movie files what mmal_container accepts\n");

    return -1;
}

int main(int ac, char **av)
{
    struct player_context context;
    MMAL_STATUS_T status;

    memset(&context, 0, sizeof(struct player_context));

    int opt = -1;
    while ((opt = getopt_long(ac, av, "r:l::L", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                context.rotation = atoi(optarg);
                break;
            case 'l':
                if (optarg == NULL)
                    context.loop = -1;
                else
                    context.loop = atoi(optarg);
                break;
            case 'L':
                context.loop_overall = 1;
                break;
            case '?':
            default:
                return usage(ac, av);
        }
    }

    if (optind == ac) {
        return usage(ac, av);
    }

    context.ac = ac;
    context.av = av;
    context.ai = optind;

    bcm_host_init();
    vcos_semaphore_create(&context.sem_event, "chain_player.events", 0);

    context.current_iter = context.loop;

    uint32_t screen_width, screen_height;
    graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);

    blank_background_start(&context.bb, 64, screen_width, screen_height);

    context.player = make_player(&context, context.av[context.ai]);
    if(context.player == NULL) {
        goto error;
    }
    mmal_player_start(context.player);

    while(1) {
        vcos_semaphore_wait(&context.sem_event);

        int exit_reason = context.reason;
        if(exit_reason == mmal_player_TERMINATED)
            break;
        if(exit_reason == mmal_player_ERROR) {
            fprintf(stderr, "exit reason: error\n");
            break;
        }

        if(exit_reason == mmal_player_EOS) {
            fprintf(stderr, "exit reason: EOS received\n");
            break;
        }
    }
    mmal_player_stop(context.player);
    mmal_player_join(context.player);

error:
    blank_background_stop(&context.bb);

    vcos_semaphore_delete(&context.sem_event);

    mmal_player_destroy(context.player);

    bcm_host_deinit();

    return 0;
}
