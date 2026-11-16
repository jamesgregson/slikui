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
  test_text.cpp
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

  # GLFW event translation on a host-owned window (no GL needed).
  add_executable(slikui_glfw_input glfw_input.cpp)
  target_link_libraries(slikui_glfw_input PRIVATE slikui glfw)
  add_test(NAME slikui_glfw_input COMMAND slikui_glfw_input)
endif()

# SDL3 event translation through SDL's real event queue, dummy video
# driver (no display needed).
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
  add_executable(slikui_sdl_input sdl_input.cpp)
  target_link_libraries(slikui_sdl_input PRIVATE slikui SDL3::SDL3)
  add_test(NAME slikui_sdl_input COMMAND slikui_sdl_input)

  # gl_parity.cpp again, with the GL context from SdlWindow (skips without
  # a display).
  add_executable(slikui_sdl_gl_parity gl_parity.cpp)
  target_compile_definitions(slikui_sdl_gl_parity PRIVATE SLIKUI_PARITY_SDL=1)
  target_link_libraries(slikui_sdl_gl_parity PRIVATE slikui SDL3::SDL3)
  if(APPLE)
    target_link_libraries(slikui_sdl_gl_parity PRIVATE "-framework OpenGL")
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(slikui_sdl_gl_parity PRIVATE OpenGL::GL)
  endif()
  add_test(NAME slikui_sdl_gl_parity COMMAND slikui_sdl_gl_parity)
endif()
