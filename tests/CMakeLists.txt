add_executable(fuhglgraphics_tests
    main.cpp
    test_version.cpp
    test_render.cpp
    test_scene.cpp
    test_io.cpp
    test_particles.cpp
    test_anim.cpp
)
target_link_libraries(fuhglgraphics_tests PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_tests COMMAND fuhglgraphics_tests)

# "unit": pure CPU tests, no GL context required.
set_tests_properties(fuhglgraphics_tests PROPERTIES LABELS "unit")

# Headless GL smoke test for the G3 deferred renderer (label "gl_context"):
# renders via a surfaceless EGL context and verifies pixels. Skips (exit code 77)
# when no GL >= 3.3 context is available.
add_executable(fuhglgraphics_gl_smoke_render gl_smoke_render.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_render PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_render COMMAND fuhglgraphics_gl_smoke_render)
set_tests_properties(fuhglgraphics_gl_smoke_render PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G9 scene graph (label "gl_context"): builds a
# scene::Scene, fills a FrameData, applies the merged effects to the G3 renderer
# and verifies frame contents + pixels. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_scene gl_smoke_scene.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_scene PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_scene COMMAND fuhglgraphics_gl_smoke_scene)
set_tests_properties(fuhglgraphics_gl_smoke_scene PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G4 glTF loader (label "gl_context"): parses a generated
# .glb fixture, instantiates it into a scene::Scene (GL meshes/materials), renders through
# the G3 DeferredRenderer and verifies pixels + per-item material IDs.
# Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_gltf gl_smoke_gltf.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_gltf PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_gltf COMMAND fuhglgraphics_gl_smoke_gltf)
set_tests_properties(fuhglgraphics_gl_smoke_gltf PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G5 particle system (label "gl_context"): creates a
# ParticleSystem (compute path on GL >= 4.3, CPU fallback below), simulates 1 s, renders
# through the G3 renderer's particles pass and verifies the frame gets brighter.
# Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_particles gl_smoke_particles.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_particles PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_particles COMMAND fuhglgraphics_gl_smoke_particles)
set_tests_properties(fuhglgraphics_gl_smoke_particles PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G6 animation module (label "gl_context"): parses the
# fixture's animation clip + skin, plays the clip, renders two frames at different times
# (images must differ) and checks CPU skinning moves weighted vertices.
# Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_anim gl_smoke_anim.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_anim PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_anim COMMAND fuhglgraphics_gl_smoke_anim)
set_tests_properties(fuhglgraphics_gl_smoke_anim PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)
