include(FetchContent)

# SDL2
find_package(SDL2 QUIET)
if (NOT ${SDL2_FOUND})
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.32.8
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(SDL2)
endif()

# ZLIB
find_package(ZLIB QUIET)
if (NOT ${ZLIB_FOUND})
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(zlib)
    
    # Create ZLIB alias for compatibility
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_LIBRARY zlibstatic)
        set(ZLIB_LIBRARIES zlibstatic)
    endif()
endif()