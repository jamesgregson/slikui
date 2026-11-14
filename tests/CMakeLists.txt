add_executable(slikui_tests
  main.cpp
  test_core.cpp
  test_layout.cpp
  test_widgets.cpp
  test_editors.cpp
  test_menus.cpp
  test_data.cpp
  test_shells.cpp
  test_theme.cpp
  test_font.cpp
  test_golden.cpp
  test_input.cpp
  test_textures.cpp
  test_batcher.cpp)
target_link_libraries(slikui_tests PRIVATE slikui)

add_test(NAME slikui_tests COMMAND slikui_tests)

# Compile every public header standalone to catch include-order issues.
add_executable(slikui_smoke smoke.cpp)
target_link_libraries(slikui_smoke PRIVATE slikui)
add_test(NAME slikui_smoke COMMAND slikui_smoke)

# OpenglSink vs software::Sink pixel comparison, offscreen. Needs GLFW +
# system OpenGL to build; prints SKIP (and passes) without a display.
find_package(glfw3 QUIET)
if(glfw3_FOUND AND (APPLE OR UNIX))
  add_executable(slikui_gl_parity gl_parity.cpp)
  target_link_libraries(slikui_gl_parity PRIVATE slikui glfw)
  if(APPLE)
    target_link_libraries(slikui_gl_parity PRIVATE "-framework OpenGL")
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(slikui_gl_parity PRIVATE OpenGL::GL)
  endif()
  add_test(NAME slikui_gl_parity COMMAND slikui_gl_parity)
endif()
