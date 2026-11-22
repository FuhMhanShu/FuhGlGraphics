add_executable(fuhglgraphics_tests
    main.cpp
    test_version.cpp
    test_render.cpp
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
