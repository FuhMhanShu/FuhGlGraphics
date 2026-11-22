add_executable(fuhglgraphics_tests
    main.cpp
    test_version.cpp
)
target_link_libraries(fuhglgraphics_tests PRIVATE fuhglgraphics)
add_test(NAME fuhglgraphics_tests COMMAND fuhglgraphics_tests)

# "unit": pure CPU tests, no GL context required.
set_tests_properties(fuhglgraphics_tests PROPERTIES LABELS "unit")
