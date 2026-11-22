# Screenshot tool: renders the G3 demo scene headless (surfaceless EGL) and writes a
# PNG — used for the reference images in docs/images/ (locally and by the CI docs job).
add_executable(fuhglgraphics_screenshot screenshot/main.cpp)
target_link_libraries(fuhglgraphics_screenshot PRIVATE fuhglgraphics)

# G9 scene-screenshot tool: same, but the demo scene is built as a scene::Scene graph
# (components, instancing, per-node effects) — produces the G9 tutorial images.
add_executable(fuhglgraphics_scene_screenshot scene_screenshot/main.cpp)
target_link_libraries(fuhglgraphics_scene_screenshot PRIVATE fuhglgraphics)
