cmake_minimum_required(VERSION 3.20)
project(FuhGlGraphics VERSION 0.1.0 LANGUAGES CXX)

option(FUHGLGRAPHICS_BUILD_TESTS "Build FuhGlGraphics tests" ON)
option(FUHGLGRAPHICS_BUILD_EXAMPLES "Build FuhGlGraphics examples (screenshot tool)" ON)
option(FUHGLGRAPHICS_WITH_QT "Use Qt6 for editors/tools (OFF = Qt-free headless core)" ON)
option(FUHGLGRAPHICS_ENABLE_HARDENING "Compiler/linker hardening (stack protector, FORTIFY, RELRO) for GNU/Clang" ON)

# --- FuhGLCore dependency (resolution order, see docs/REQUIREMENTS.md G1) ---------
# 1) -DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>  -> add_subdirectory (local checkout)
# 2) find_package(FuhGLCore)               -> installed package
# 3) FetchContent from github.com/FuhMhanShu/FuhGLCore
#    (git ref: FUHGLGRAPHICS_FUHGLCORE_GIT_TAG, default "main")
set(FUHGLGRAPHICS_FUHGLCORE_DIR "" CACHE PATH
    "Path to a local FuhGLCore checkout (add_subdirectory); takes precedence over find_package/FetchContent")
set(FUHGLGRAPHICS_FUHGLCORE_GIT_TAG "main" CACHE STRING
    "Git ref of FuhGLCore used by the FetchContent fallback")

if(FUHGLGRAPHICS_FUHGLCORE_DIR)
    set(FUHGL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FUHGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${FUHGLGRAPHICS_FUHGLCORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/_deps/fuhglcore)
else()
    find_package(FuhGLCore QUIET)
    if(NOT TARGET FuhGLCore::fuhglcore)
        include(FetchContent)
        FetchContent_Declare(FuhGLCore
            GIT_REPOSITORY https://github.com/FuhMhanShu/FuhGLCore.git
            GIT_TAG ${FUHGLGRAPHICS_FUHGLCORE_GIT_TAG}
        )
        set(FUHGL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(FUHGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(FuhGLCore)
    endif()
endif()

# --- library ----------------------------------------------------------------------
add_library(fuhglgraphics STATIC
    src/version.cpp
    src/render/deferred_renderer.cpp
    src/render/debug_hud.cpp
    src/render/ssao.cpp
    src/scene/component.cpp
    src/scene/effects.cpp
    src/scene/node.cpp
    src/scene/scene.cpp
    src/scene/picking.cpp
    src/scene/serialize.cpp
    src/io/json.cpp
    src/io/gltf.cpp
    src/particles/particle_system.cpp
    src/anim/animation.cpp
    src/anim/gpu_skinner.cpp
    src/probes/light_probes.cpp
    src/water/water_surface.cpp
    src/procedural/marching_cubes.cpp
    src/procedural/lsystem.cpp
    src/procedural/wfc.cpp
    src/procedural/voronoi.cpp
    src/procedural/sdf.cpp
    src/procedural/creature.cpp
    src/procedural/creaturegen.cpp
    src/procedural/worldgen.cpp
    src/procedural/house.cpp
    src/procedural/citygen.cpp
    src/procedural/plant.cpp
)
add_library(FuhGlGraphics::fuhglgraphics ALIAS fuhglgraphics)

target_include_directories(fuhglgraphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(fuhglgraphics PUBLIC cxx_std_17)
set_target_properties(fuhglgraphics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(fuhglgraphics PUBLIC FUHGLGRAPHICS_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(fuhglgraphics PUBLIC FuhGLCore::fuhglcore)

# --- Qt6 (editors/tools only; the library core stays Qt-free, see G2) --------------
if(FUHGLGRAPHICS_WITH_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    set_target_properties(fuhglgraphics PROPERTIES AUTOMOC ON)
    # Qt code lives only in editor/ (the core in src/ stays Qt-free); the header is listed
    # so AUTOMOC picks up its Q_OBJECT.
    target_sources(fuhglgraphics PRIVATE
        editor/gl_viewport_widget.cpp
        include/fuhglgraphics/editor/gl_viewport_widget.hpp
    )
    target_link_libraries(fuhglgraphics PUBLIC Qt6::Widgets Qt6::OpenGLWidgets)
    target_compile_definitions(fuhglgraphics PUBLIC FUHGLGRAPHICS_WITH_QT=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fuhglgraphics PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- hardening flags (GNU/Clang, Linux; opt out with -DFUHGLGRAPHICS_ENABLE_HARDENING=OFF) ---
if(FUHGLGRAPHICS_ENABLE_HARDENING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-fstack-protector-strong" FUHGLGRAPHICS_HAS_STACK_PROTECTOR)
    if(FUHGLGRAPHICS_HAS_STACK_PROTECTOR)
        target_compile_options(fuhglgraphics PRIVATE -fstack-protector-strong)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(CheckLinkerFlag)
        check_cxx_compiler_flag("-fstack-clash-protection" FUHGLGRAPHICS_HAS_STACK_CLASH)
        if(FUHGLGRAPHICS_HAS_STACK_CLASH)
            target_compile_options(fuhglgraphics PRIVATE -fstack-clash-protection)
        endif()
        # _FORTIFY_SOURCE only does something with optimization enabled
        target_compile_options(fuhglgraphics PRIVATE
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=2>)
        check_linker_flag(CXX "-Wl,-z,relro,-z,now" FUHGLGRAPHICS_HAS_FULL_RELRO)
        if(FUHGLGRAPHICS_HAS_FULL_RELRO)
            target_link_options(fuhglgraphics PUBLIC -Wl,-z,relro,-z,now)
        endif()
        check_linker_flag(CXX "-Wl,-z,noexecstack" FUHGLGRAPHICS_HAS_NOEXECSTACK)
        if(FUHGLGRAPHICS_HAS_NOEXECSTACK)
            target_link_options(fuhglgraphics PUBLIC -Wl,-z,noexecstack)
        endif()
    endif()
endif()

# --- install / package export -------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS fuhglgraphics
    EXPORT FuhGlGraphicsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
install(EXPORT FuhGlGraphicsTargets
    NAMESPACE FuhGlGraphics::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FuhGlGraphics
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/FuhGlGraphicsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    cmake/FuhGlGraphicsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/FuhGlGraphicsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FuhGlGraphics
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/FuhGlGraphicsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/FuhGlGraphicsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FuhGlGraphics
)

# --- tests --------------------------------------------------------------------------
if(FUHGLGRAPHICS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- examples -----------------------------------------------------------------------
if(FUHGLGRAPHICS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
