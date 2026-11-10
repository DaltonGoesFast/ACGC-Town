cmake_minimum_required(VERSION 3.22)
project(acgc_town_android C)

# SDL2 is downloaded at configure time (zlib). It is not committed.
include(FetchContent)
FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz
    URL_HASH SHA256=f59adf36a0fcf4c94198e7d3d776c1b3824211ab7aeebeb31fe19836661196aa
)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

add_library(abi SHARED abi.c)
target_link_libraries(abi log)

add_library(main SHARED android_main.c)
target_link_libraries(main SDL2 GLESv3 log)

# The full game (pc/CMakeLists.txt) is not part of this target. See docs/ANDROID.md.
# When it is linked, build it as armeabi-v7a with TARGET_PC and without PC_ALLOW_64BIT.
