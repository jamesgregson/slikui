add_executable(silk_tests
  main.cpp
  test_core.cpp
  test_layout.cpp
  test_widgets.cpp
  test_theme.cpp
  test_golden.cpp)
target_link_libraries(silk_tests PRIVATE silk)

add_test(NAME silk_tests COMMAND silk_tests)

# Compile every public header standalone to catch include-order issues.
add_executable(silk_smoke smoke.cpp)
target_link_libraries(silk_smoke PRIVATE silk)
add_test(NAME silk_smoke COMMAND silk_smoke)
