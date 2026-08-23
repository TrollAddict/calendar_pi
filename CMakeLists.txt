cmake_minimum_required(VERSION 3.13)
project(calendar_pi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(Threads REQUIRED)

add_executable(calendar_pi
    src/main.c
    src/drm_display.c
    src/gl_renderer.c
    src/font.c
    src/calendar_model.c
    src/input.c
    src/calendar_view.c
    src/gcal_colors.c
    src/config_store.c
    src/event_store.c
    src/oauth_refresh.c
    src/gcal_client.c
    src/gcal_sync.c
    third_party/cJSON.c
)

# Vendored third-party code isn't held to this project's own warning bar.
set_source_files_properties(third_party/cJSON.c PROPERTIES COMPILE_OPTIONS "-w")

target_include_directories(calendar_pi PRIVATE
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    third_party
)

target_link_libraries(calendar_pi PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
    m
)

target_compile_options(calendar_pi PRIVATE
    -Wall -Wextra
    ${DRM_CFLAGS_OTHER} ${GBM_CFLAGS_OTHER} ${EGL_CFLAGS_OTHER} ${GLESV2_CFLAGS_OTHER} ${CURL_CFLAGS_OTHER}
)
