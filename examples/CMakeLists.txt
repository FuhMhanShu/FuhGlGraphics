# Screenshot tool: renders the G3 demo scene headless (surfaceless EGL) and writes a
# PNG — used for the reference images in docs/images/ (locally and by the CI docs job).
add_executable(fuhglgraphics_screenshot screenshot/main.cpp)
target_link_libraries(fuhglgraphics_screenshot PRIVATE fuhglgraphics)

# G9 scene-screenshot tool: same, but the demo scene is built as a scene::Scene graph
# (components, instancing, per-node effects) — produces the G9 tutorial images.
add_executable(fuhglgraphics_scene_screenshot scene_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_scene_screenshot PRIVATE fuhglgraphics)

# Landscape screenshot tool: G9 scene-graph landscape (terrain + many two-mesh tree
# nodes) with a procedural cloud sky cubemap, sun disk, fog and god rays.
add_executable(fuhglgraphics_landscape_screenshot landscape_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_landscape_screenshot PRIVATE fuhglgraphics)

# G4 glTF demo: loads a .gltf/.glb model (io::loadGltf + io::instantiateGltf into a G9
# scene graph), stages it on a plaza with sun/sky/IBL and renders a PNG headless.
add_executable(fuhglgraphics_gltf_demo gltf_demo/main.cpp)
target_link_libraries(fuhglgraphics_gltf_demo PRIVATE fuhglgraphics)

# G5 particle demo: GPU particle systems (compute on GL >= 4.3, CPU fallback) as G9
# ParticleEmitterComponents through the G3 renderer's particles pass — campfire embers,
# smoke plume, pollen motes — headless to PNG.
add_executable(fuhglgraphics_particles_screenshot particles_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_particles_screenshot PRIVATE fuhglgraphics)

# G6 animation demo: skeletal arm (CPU skinning with the joint palette) + waving flag
# (TRS clip) through the G3 renderer, headless to PNG.
add_executable(fuhglgraphics_animation_screenshot animation_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_animation_screenshot PRIVATE fuhglgraphics)

# G7 light-probe demo: colored-wall courtyard, SH probe grid baked on the CPU and sampled
# in the G3 lighting pass (Effect::LightProbes) — color bleeding in the shadows.
add_executable(fuhglgraphics_probes_screenshot probes_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_probes_screenshot PRIVATE fuhglgraphics)

# G13 water demo: lake scene (terrain basin + Gerstner-wave water surface with planar
# reflection, refraction, fresnel, crest/shore foam and sun glint) as a G9 scene graph —
# WaterComponent -> FrameData::water -> DeferredRenderer with Effect::Water — headless to PNG.
add_executable(fuhglgraphics_water_screenshot water_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_water_screenshot PRIVATE fuhglgraphics)
