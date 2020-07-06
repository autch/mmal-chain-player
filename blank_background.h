#ifndef MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H
#define MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H

#include "bcm_host.h"
#include "interface/mmal/mmal.h"

struct blank_background
{
    int layer;
    uint32_t screen_width, screen_height;

    MMAL_COMPONENT_T* video_render;
    MMAL_POOL_T* pool;
};

int blank_background_start(struct blank_background* context, int layer, int screen_width, int screen_height);
int blank_background_stop(struct blank_background* context);

#endif //MMAL_CHAIN_PLAYER_BLANK_BACKGROUND_H
