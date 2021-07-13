add_subdirectory(cocoa)

if(NOT OPTION_DEDICATED)
    add_files(
        allegro_v.cpp
        allegro_v.h
        CONDITION Allegro_FOUND
    )

    add_files(
        opengl.cpp
        opengl.h
        CONDITION OPENGL_FOUND
    )

    add_files(
        d3d11.cpp
        d3d11.h
        CONDITION D3D11_FOUND
    )

    add_files(
        sdl_v.cpp
        sdl_v.h
        CONDITION SDL_FOUND
    )

    add_files(
        sdl2_v.cpp
        sdl2_v.h
        sdl2_default_v.cpp
        sdl2_default_v.h
        CONDITION SDL2_FOUND
    )

    add_files(
        sdl2_opengl_v.cpp
        sdl2_opengl_v.h
        CONDITION SDL2_FOUND AND OPENGL_FOUND
    )

    add_files(
        win32_v.cpp
        win32_v.h
        CONDITION WIN32
    )
endif()

add_files(
    dedicated_v.cpp
    dedicated_v.h
    null_v.cpp
    null_v.h
    video_driver.cpp
    video_driver.hpp
)
