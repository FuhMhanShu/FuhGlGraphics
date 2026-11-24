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

# G8 decal demo: plaza with bullet-hole decals (ground + wall, material-ID gated) and a
# ground marking, projected into the G-buffer by the G3 decal pass (Effect::Decals).
add_executable(fuhglgraphics_decals_screenshot decals_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_decals_screenshot PRIVATE fuhglgraphics)

# G10 demo: DoF with a hexagonal bokeh kernel (Effect::Dof) + debug HUD (Effect::DebugHud).
add_executable(fuhglgraphics_dof_screenshot dof_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_dof_screenshot PRIVATE fuhglgraphics)

# G13 water demo: lake scene (terrain basin + Gerstner-wave water surface with planar
# reflection, refraction, fresnel, crest/shore foam and sun glint) as a G9 scene graph —
# WaterComponent -> FrameData::water -> DeferredRenderer with Effect::Water — headless to PNG.
add_executable(fuhglgraphics_water_screenshot water_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_water_screenshot PRIVATE fuhglgraphics)

# G14 GPU-skinning demo: skeletal arm skinned on the GPU (anim::GpuSkinner palette SSBO in
# the G3 G-buffer pass, Effect::GpuSkinning, GL >= 4.3) — headless to PNG.
add_executable(fuhglgraphics_gpu_skinning_screenshot gpu_skinning_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_gpu_skinning_screenshot PRIVATE fuhglgraphics)

# G15 SSAO demo: courtyard (crates + wall) where the G3 "ssao" post pass darkens the
# contact corners/edges (Effect::Ssao) — headless to PNG.
add_executable(fuhglgraphics_ssao_screenshot ssao_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_ssao_screenshot PRIVATE fuhglgraphics)

# G16 SSR demo: showroom with a mirror floor reflecting an emissive pillar + crates — the G3
# "ssr" post pass raymarches the G-buffer depth (Effect::Ssr) — headless to PNG.
add_executable(fuhglgraphics_ssr_screenshot ssr_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_ssr_screenshot PRIVATE fuhglgraphics)

# G17 bloom demo: dusk plaza with HDR-bright lamps + sun disk — the G3 "bloom" post pass
# (bright-pass + blur + add before TAA, Effect::Bloom, adjustable threshold) — headless to PNG.
add_executable(fuhglgraphics_bloom_screenshot bloom_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_bloom_screenshot PRIVATE fuhglgraphics)

# G18 picking demo: plaza with crates — scene::pick() ray-casts a pixel (AABB + triangle
# tests on MeshEntry::meshData) and a marker cube is rendered at the hit point — headless to PNG.
add_executable(fuhglgraphics_picking_screenshot picking_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_picking_screenshot PRIVATE fuhglgraphics)

# G19 serialization demo: builds a scene, renders it, saves it to .json, injects procedural
# mesh specs, loads it back with the default shape resolver and renders the loaded scene —
# both PNGs must match (round-trip) — headless to PNG.
add_executable(fuhglgraphics_serialization_screenshot serialization_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_serialization_screenshot PRIVATE fuhglgraphics)

# G20 terrain + LOD demo: TerrainComponent (F38/F41 heightmap terrain as a LodMesh) with crates
# placed via heightAt(), rendered near (LOD 0) and far (higher LOD level) — headless to PNG.
add_executable(fuhglgraphics_terrain_screenshot terrain_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_terrain_screenshot PRIVATE fuhglgraphics)

# G21 volumetric fog demo: terrain scene rendered with Effect::VolumetricFog off / on / dense
# (raymarched 3D-noise volume, height falloff + Henyey-Greenstein sun scattering) — headless to PNG.
add_executable(fuhglgraphics_volumetric_screenshot volumetric_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_volumetric_screenshot PRIVATE fuhglgraphics)

# G22 wetness demo: plaza with SSR, rendered dry / wet / puddly (Effect::Wetness modulates
# albedo+roughness in the lighting pass + puddle mask + SSR boost) — headless to PNG.
add_executable(fuhglgraphics_wetness_screenshot wetness_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_wetness_screenshot PRIVATE fuhglgraphics)

# G23 tonemap demo: dusk plaza with HDR lamps, rendered per operator (ACES/Reinhard/Uncharted2/
# linear) + one shot with bloom on and a high adjustable bloom threshold — headless to PNG.
add_executable(fuhglgraphics_tonemap_screenshot tonemap_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_tonemap_screenshot PRIVATE fuhglgraphics)

# Sunrise showcase: landscape with lake (G20 terrain+LOD, carved basin, G13 water), vegetation
# (trees/rocks/reeds/lily pads), dock with waving flag (G6), wear decal (G8), pollen+mist
# particles (G5), sunrise sky + IBL, low sun with CSM shadows + god rays, SSAO/SSR/bloom/
# volumetric mist (G21)/dew wetness (G22), TAA+upscale+ACES — two shots, headless to PNG.
add_executable(fuhglgraphics_sunrise_screenshot sunrise_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_sunrise_screenshot PRIVATE fuhglgraphics)

# Parallax-mapping (POM) showcase: brick house in sunshine with procedural brick
# albedo/height/normal textures; the G3 G-buffer pass marches the height field
# (MeshEntry::heightMap) and offsets the UVs at grazing angles 
# wide shot + wall closeup with POM vs without, headless to PNG.
add_executable(fuhglgraphics_parallax_house_screenshot parallax_house_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_parallax_house_screenshot PRIVATE fuhglgraphics)

# G24/G25 post-effect showcase: dusk plaza with lens flare + anamorphic streaks on the low sun
# (on vs off) and a daylight courtyard with HBAO vs SSAO (contact-corner occlusion) — headless to PNG.
add_executable(fuhglgraphics_postfx_screenshot postfx_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_postfx_screenshot PRIVATE fuhglgraphics)
# G24 procedural-noise showcase: terrain heightmap carved with procedural::Noise
# (domain-warped ridged fBm + simplex detail + carved lake basin), G13 lake water,
# cloud-sky cubemap from the same noise source + IBL, sun + CSM shadows + god rays,
# SSAO + volumetric fog + wetness + bloom, TAA + upscale + ACES — wide + ridge shots.
add_executable(fuhglgraphics_noise_screenshot noise_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_noise_screenshot PRIVATE fuhglgraphics)

# G25 marching-cubes showcase: cave terrain extracted from a G24-noise volume with chunked
# marching cubes (one MeshData per chunk as MeshEntries of one node), emissive crystal blobs
# with bloom, dusk sky + IBL, sun + CSM shadows + god rays, SSAO + volumetric fog,
# TAA + upscale + ACES — cave overview + crystal closeup, headless to PNG.
add_executable(fuhglgraphics_marching_cubes_screenshot marching_cubes_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_marching_cubes_screenshot PRIVATE fuhglgraphics)

# G26 L-system showcase: forest of L-system trees (3 variants) + ferns on a G24-noise terrain,
# 4000 instanced grass blades snapped via Instancing::heightFn, noon sun + CSM shadows + god
# rays, day-sky cubemap + IBL, SSAO + volumetric fog, TAA + upscale + ACES — forest overview +
# tree closeup, headless to PNG.
add_executable(fuhglgraphics_lsystem_screenshot lsystem_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_lsystem_screenshot PRIVATE fuhglgraphics)

# G27 WFC showcase: dungeon layout solved by procedural::Wfc (edge-tag tiles + rotations +
# backtracking) instantiated as merged per-(tile,rot) scene nodes, torches (emissive + point
# lights + bloom), wear decals (G8), dim directional light + CSM shadows, SSAO, volumetric
# fog, TAA + upscale + ACES — interior + top-down overview, headless to PNG.
add_executable(fuhglgraphics_wfc_screenshot wfc_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_wfc_screenshot PRIVATE fuhglgraphics)

# G28 Voronoi showcase: shattered plaza (procedural::Voronoi2D cells extruded with gaps over an
# emissive underlay -> glowing cracks + bloom) + crystal-shard cluster (height jitter, metallic,
# point light), sunset sky cubemap (G24 noise clouds) + IBL, sun + CSM shadows + god rays,
# SSAO + volumetric fog, TAA + upscale + ACES — wide shot + closeup, headless to PNG.
add_executable(fuhglgraphics_voronoi_screenshot voronoi_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_voronoi_screenshot PRIVATE fuhglgraphics)
# G29 SDF/fractal showcase: Mandelbulb (power 8 DE) meshed via meshFromSdf + G25 marching cubes
# (56^3), floating emissive sculpture + satellite bulbs, violet core light (bloom), warm sun +
# CSM + god rays, dusk sky + IBL, SSAO + volumetric fog, TAA + upscale + ACES — wide + closeup.
add_executable(fuhglgraphics_mandelbulb_screenshot mandelbulb_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_mandelbulb_screenshot PRIVATE fuhglgraphics)

# G30 procedural-creature showcase: quadruped + reptile from a skeleton graph -> SDF (smooth-min
# capsules/ellipsoids + simplex detail) -> G25 marching cubes with curvature vertex colors,
# baked albedo/normal maps from object-space 3D noise (scales, stripes, belly), emissive eyes,
# dusk sun + CSM + god rays, sky + IBL, SSAO + bloom + volumetric fog, TAA + upscale + ACES —
# wide + closeup, headless to PNG.
add_executable(fuhglgraphics_creature_screenshot creature_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_creature_screenshot PRIVATE fuhglgraphics)

# G31 world-generator showcase: procedural::generateWorld() builds a landscape (G24 noise
# heightmap + lake basin + G26 L-system trees) and a city (G27 WFC layout + G28 district
# plates + building boxes), attached to scene::Scene nodes; sun + CSM shadows + gradient sky,
# TAA + upscale + ACES — landscape overview, city overview + street level, headless to PNG.
add_executable(fuhglgraphics_worldgen_screenshot worldgen_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_worldgen_screenshot PRIVATE fuhglgraphics)
# G32/G33/G34 post-effect showcase: chromatic aberration (dusk lamps, on/off), subsurface
# scattering (backlit thin panel, on/off) and screen-space GI (red-wall bounce on a box,
# on/off) — headless to PNG.
add_executable(fuhglgraphics_postfx2_screenshot postfx2_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_postfx2_screenshot PRIVATE fuhglgraphics)
# G36 ice-cave showcase: procedural SDF cave (marching cubes) with a campfire INSIDE the
# cave (G35 realistic flame particles + embers + smoke), ice crystals (SSR + SSS), a water
# puddle (planar reflection), HBAO, bloom, volumetric fog, CSM + point shadows, dark entrance
# glow + IBL — wide + crystals + entrance shots, headless to PNG.
add_executable(fuhglgraphics_cave_screenshot cave_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_cave_screenshot PRIVATE fuhglgraphics)
