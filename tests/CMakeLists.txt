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
    test_terrain_lod.cpp
    test_volumetric.cpp
    test_wetness.cpp
    test_tonemap.cpp
    test_parallax.cpp
    test_hbao.cpp
    test_procedural_noise.cpp
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

# G20: terrain LOD smoke test (near/far camera -> lastLodLevel, lodEnabled=false -> -1).
add_executable(fuhglgraphics_gl_smoke_terrain gl_smoke_terrain.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_terrain PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_terrain COMMAND fuhglgraphics_gl_smoke_terrain)
set_tests_properties(fuhglgraphics_gl_smoke_terrain PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# G21: volumetric fog smoke test (fog on vs off -> measurable difference, sky fogged too).
add_executable(fuhglgraphics_gl_smoke_volumetric gl_smoke_volumetric.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_volumetric PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_volumetric COMMAND fuhglgraphics_gl_smoke_volumetric)
set_tests_properties(fuhglgraphics_gl_smoke_volumetric PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# G22: wetness smoke test (dry vs wet with SSR on -> measurable difference).
add_executable(fuhglgraphics_gl_smoke_wetness gl_smoke_wetness.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_wetness PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_wetness COMMAND fuhglgraphics_gl_smoke_wetness)
set_tests_properties(fuhglgraphics_gl_smoke_wetness PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# G23: tonemap-operator smoke test (ACES/Reinhard/Uncharted2/linear frames differ).
add_executable(fuhglgraphics_gl_smoke_tonemap gl_smoke_tonemap.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_tonemap PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_tonemap COMMAND fuhglgraphics_gl_smoke_tonemap)
set_tests_properties(fuhglgraphics_gl_smoke_tonemap PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# Parallax occlusion mapping smoke test (label "gl_context"): grazing view on a brick wall
# with a height map \u2014 POM on vs off must differ measurably. Skips (exit code 77) without GL.
add_executable(fuhglgraphics_gl_smoke_parallax gl_smoke_parallax.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_parallax PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_parallax COMMAND fuhglgraphics_gl_smoke_parallax)
set_tests_properties(fuhglgraphics_gl_smoke_parallax PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# G24 lens flare + anamorphic smoke test (label "gl_context"): sun in front of the camera —
# flare/anamorphic on vs off must differ, and the anamorphic streak must concentrate on the
# sun's screen row. Skips (exit code 77) without GL >= 3.3.
add_executable(fuhglgraphics_gl_smoke_flare gl_smoke_flare.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_flare PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_flare COMMAND fuhglgraphics_gl_smoke_flare)
set_tests_properties(fuhglgraphics_gl_smoke_flare PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)

# G25 HBAO smoke test (label "gl_context"): box on ground — HBAO on vs off must differ and
# the changed pixels must mostly get darker (AO is multiplicative). Skips (exit code 77).
add_executable(fuhglgraphics_gl_smoke_hbao gl_smoke_hbao.cpp)
target_link_libraries(fuhglgraphics_gl_smoke_hbao PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_gl_smoke_hbao COMMAND fuhglgraphics_gl_smoke_hbao)
set_tests_properties(fuhglgraphics_gl_smoke_hbao PROPERTIES LABELS "gl_context" SKIP_RETURN_CODE 77)
# Qt editor-widget unit test (only with FUHGLGRAPHICS_WITH_QT=ON): runs offscreen
# (QT_QPA_PLATFORM=offscreen), no GL context required — label "unit".
if(FUHGLGRAPHICS_WITH_QT)
    add_executable(fuhglgraphics_editor_tests test_editor_widget.cpp)
    target_link_libraries(fuhglgraphics_editor_tests PRIVATE fuhglgraphics)
    add_test(NAME fuhglgraphics_editor_tests COMMAND fuhglgraphics_editor_tests)
    set_tests_properties(fuhglgraphics_editor_tests PROPERTIES LABELS "unit")
endif()
