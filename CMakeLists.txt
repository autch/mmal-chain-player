cmake_minimum_required(VERSION 3.10)
project(mmal-chain-player C)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(VC_ROOT "/opt/vc")
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${VC_ROOT}/lib/pkgconfig")
set(OPT_VC_SRC "${VC_ROOT}/src/hello_pi")
set(OPT_VC_INCLUDE "${VC_ROOT}/include")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(BCM_HOST REQUIRED bcm_host)
pkg_check_modules(MMAL REQUIRED mmal)

include_directories(
        ${BCM_HOST_INCLUDE_DIRS}
        ${MMAL_INCLUDE_DIRS}
)

link_directories(
        ${BCM_HOST_LIBRARY_DIRS}
        ${MMAL_LIBRARY_DIRS}
)

option(SEAMLESS_LOOP "enable smooth looping trial" OFF)
if(SEAMLESS_LOOP)
    message("Enabling experimental seamless looping")
    add_definitions(-DSEAMLESS_LOOP)
endif(SEAMLESS_LOOP)

add_definitions(
)
add_compile_options(
        -fPIC
        -Wall
)

set(CMAKE_C_FLAGS_DEBUG "-g -O3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

SET(COMPILE_DEFINITIONS -Werror -Wall)

add_executable(mmal-chain-player
    mmal-chain-player.c
    blank_background.c blank_background.h
    mmal-player-pipeline.c mmal-player-pipeline.h
)

target_link_libraries(mmal-chain-player
    ${BCM_HOST_LIBRARIES}
    ${MMAL_LIBRARIES}
    Threads::Threads
)
