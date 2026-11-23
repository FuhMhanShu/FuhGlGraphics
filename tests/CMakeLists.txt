add_executable(fuhglgraphics_tests
    main.cpp
    test_version.cpp
    test_render.cpp
    test_scene.cpp
    test_io.cpp
    test_particles.cpp
    test_anim.cpp
    test_probes.cpp
    test_water.cpp
    test_ssao.cpp
    test_ssr.cpp
    test_bloom.cpp
    test_picking.cpp
    test_serialize.cpp
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

# Headless GL smoke test for the G7 light probes (label "gl_context"): bakes an SH probe
# grid over a colored-wall room on the CPU, uploads it, renders through the G3 renderer
# with Effect::LightProbes on vs off and verifies the frames differ and the shadowed
# corner gets brighter with probes. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_probes gl_smoke_probes.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_probes PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_probes COMMAND fuhglgraphics_gl_smoke_probes)
set_tests_properties(fuhglgraphics_gl_smoke_probes PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G8 decals (label "gl_context"): renders a ground quad with
# a red decal box with Effect::Decals on vs off and verifies the mark appears only when
# enabled and only on the targeted material ID. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_decals gl_smoke_decals.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_decals PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_decals COMMAND fuhglgraphics_gl_smoke_decals)
set_tests_properties(fuhglgraphics_gl_smoke_decals PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G10 upgrades (label "gl_context"): DoF (Effect::Dof blurs
# a near crate when focused far) and the debug HUD (Effect::DebugHud draws the panel
# top-left). Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_g10 gl_smoke_g10.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_g10 PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_g10 COMMAND fuhglgraphics_gl_smoke_g10)
set_tests_properties(fuhglgraphics_gl_smoke_g10 PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G13 water effect (label "gl_context"): builds a small
# scene with a water surface (Gerstner waves), renders through the G3 renderer with
# Effect::Water on vs off and verifies that the frames differ and that the reflection
# texture is non-black. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_water gl_smoke_water.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_water PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_water COMMAND fuhglgraphics_gl_smoke_water)
set_tests_properties(fuhglgraphics_gl_smoke_water PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G14 GPU skinning (label "gl_context"): a 2-joint skinned
# ribbon is rendered with Effect::GpuSkinning on (GL >= 4.3 palette SSBO) at two palettes —
# the frames must differ; with the effect off both frames are identical (CPU fallback).
# Skips (exit code 77) without GL >= 3.3 (or GL >= 4.3 for the skinning path itself).
add_executable(fuhglgraphics_gl_smoke_gpu_skin gl_smoke_gpu_skin.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_gpu_skin PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_gpu_skin COMMAND fuhglgraphics_gl_smoke_gpu_skin)
set_tests_properties(fuhglgraphics_gl_smoke_gpu_skin PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G15 SSAO (label "gl_context"): box on ground — with
# Effect::Ssao on vs off the frames must differ and the changed pixels must get darker
# (AO is multiplicative), concentrated at the contact corner. Skips (exit code 77) without GL.
add_executable(fuhglgraphics_gl_smoke_ssao gl_smoke_ssao.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_ssao PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_ssao COMMAND fuhglgraphics_gl_smoke_ssao)
set_tests_properties(fuhglgraphics_gl_smoke_ssao PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G16 SSR (label "gl_context"): mirror floor + bright emissive
# box — with Effect::Ssr on the box appears reflected in the floor region (brighter there).
# Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_ssr gl_smoke_ssr.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_ssr PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_ssr COMMAND fuhglgraphics_gl_smoke_ssr)
set_tests_properties(fuhglgraphics_gl_smoke_ssr PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G17 bloom (label "gl_context"): dark scene + HDR-bright
# emissive lamp — with Effect::Bloom on there is a halo ring around the lamp; with a very
# high bloom threshold there is none (adjustable threshold). Skips (exit code 77) no GL.
add_executable(fuhglgraphics_gl_smoke_bloom gl_smoke_bloom.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_bloom PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_bloom COMMAND fuhglgraphics_gl_smoke_bloom)
set_tests_properties(fuhglgraphics_gl_smoke_bloom PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G18 picking (label "gl_context"): picks the center pixel of
# a scene with a box (meshData set), checks the hit node/distance, then renders a marker at
# the hit point and verifies it is visible. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_picking gl_smoke_picking.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_picking PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_picking COMMAND fuhglgraphics_gl_smoke_picking)
set_tests_properties(fuhglgraphics_gl_smoke_picking PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Headless GL smoke test for the G19 scene serialization (label "gl_context"): saves a scene
# to .json, loads it back with the default shape resolver (real GL meshes), renders it and
# verifies items/lights + non-dark pixels. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_serialize gl_smoke_serialize.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_serialize PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_serialize COMMAND fuhglgraphics_gl_smoke_serialize)
set_tests_properties(fuhglgraphics_gl_smoke_serialize PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)
