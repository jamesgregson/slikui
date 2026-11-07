add_executable(slikui_tests
  main.cpp
  test_core.cpp
  test_layout.cpp
  test_widgets.cpp
  test_theme.cpp
  test_font.cpp
  test_golden.cpp)
target_link_libraries(slikui_tests PRIVATE slikui)

add_test(NAME slikui_tests COMMAND slikui_tests)

# Compile every public header standalone to catch include-order issues.
add_executable(slikui_smoke smoke.cpp)
target_link_libraries(slikui_smoke PRIVATE slikui)
add_test(NAME slikui_smoke COMMAND slikui_smoke)
