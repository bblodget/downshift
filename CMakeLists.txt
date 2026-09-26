cmake_minimum_required(VERSION 3.19)

# ---- Toolchain: PGE3 needs C++20 and, for this machine, GCC 13 -------------
# Set before project() so they take effect. Override on the command line with
# -DCMAKE_CXX_COMPILER=... if needed.
if(NOT CMAKE_CXX_COMPILER AND NOT EMSCRIPTEN AND NOT CMAKE_TOOLCHAIN_FILE AND EXISTS /usr/bin/g++-13)
    set(CMAKE_C_COMPILER   /usr/bin/gcc-13)
    set(CMAKE_CXX_COMPILER /usr/bin/g++-13)
endif()

# ---- Rename your game here (binary name, web files, zip name) --------------
set(GAME downshift)

project(${GAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---- Options ---------------------------------------------------------------
# Where the PGE3 checkout lives (needs olcPixelGameEngine3.h at its root)
set(PGE3_DIR "${CMAKE_SOURCE_DIR}/../repo/olcPixelGameEngine3" CACHE PATH "Path to olcPixelGameEngine3 repo")
option(USE_WAYLAND "Build against native Wayland instead of X11 (Linux only)" OFF)
option(USE_STB     "Use stb_image for image loading (needs stb_image*.h in PGE3_DIR)" OFF)

get_filename_component(PGE3_DIR "${PGE3_DIR}" ABSOLUTE)
if(NOT EXISTS "${PGE3_DIR}/olcPixelGameEngine3.h")
    message(FATAL_ERROR "olcPixelGameEngine3.h not found in PGE3_DIR=${PGE3_DIR}")
endif()

# ---- Target ----------------------------------------------------------------
add_executable(${GAME} src/main.cpp)
target_include_directories(${GAME} SYSTEM PRIVATE "${PGE3_DIR}" "${PGE3_DIR}/utilities")
target_include_directories(${GAME} PRIVATE src)
target_compile_options(${GAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(USE_STB)
    target_compile_definitions(${GAME} PRIVATE OLC_USE_STB_IMAGE=1)
endif()

find_package(Threads REQUIRED)

if(EMSCRIPTEN)
    # Web build: emcmake cmake -S . -B build-web && cmake --build build-web
    set_target_properties(${GAME} PROPERTIES SUFFIX ".html")
    target_link_options(${GAME} PRIVATE
        -sASYNCIFY                      # PGE3's main loop needs it on the web
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1048576
        -sEXPORTED_RUNTIME_METHODS=HEAPF32
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        -sUSE_LIBPNG=1
        -sLLD_REPORT_UNDEFINED
        --preload-file ${CMAKE_SOURCE_DIR}/assets@assets)
    target_compile_options(${GAME} PRIVATE -sUSE_LIBPNG=1)
elseif(UNIX AND NOT APPLE)
    if(USE_WAYLAND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WL REQUIRED xkbcommon wayland-client wayland-egl wayland-cursor egl)
        pkg_check_modules(LIBDECOR REQUIRED IMPORTED_TARGET libdecor-0)
        find_package(OpenGL REQUIRED)
        find_package(PNG REQUIRED)
        find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

        # Generate the two protocol bindings PGE3's Wayland host needs
        set(WLP /usr/share/wayland-protocols)
        function(wl_protocol name xml)
            set(c ${CMAKE_CURRENT_BINARY_DIR}/${name}.c)
            set(h ${CMAKE_CURRENT_BINARY_DIR}/${name}.h)
            add_custom_command(OUTPUT ${c} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${c} DEPENDS ${xml} VERBATIM)
            add_custom_command(OUTPUT ${h} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${h} DEPENDS ${xml} VERBATIM)
            add_library(wl_${name} STATIC ${c} ${h})
            target_include_directories(wl_${name} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
        endfunction()
        wl_protocol(xdg-shell    ${WLP}/stable/xdg-shell/xdg-shell.xml)
        wl_protocol(pointer-warp ${WLP}/staging/pointer-warp/pointer-warp-v1.xml)

        target_compile_definitions(${GAME} PRIVATE OLC_HOST=3)
        target_include_directories(${GAME} PRIVATE ${WL_INCLUDE_DIRS})
        target_link_libraries(${GAME} PRIVATE wl_xdg-shell wl_pointer-warp
            ${WL_LIBRARIES} PkgConfig::LIBDECOR OpenGL::GL PNG::PNG Threads::Threads)
    else()
        # X11 (also runs under XWayland). OLC_HOST left unset: header defaults to X11 on Linux.
        target_link_libraries(${GAME} PRIVATE png GL X11 Xi Threads::Threads)
    endif()
endif()

# ---- Assets: copy next to the binary so it runs from build/ ----------------
# (web builds embed them via --preload-file above)
if(NOT EMSCRIPTEN)
    add_custom_command(TARGET ${GAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${GAME}>/assets)
endif()
