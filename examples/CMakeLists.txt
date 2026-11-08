# Windowed examples need GLFW; system OpenGL provides the renderer.
# examples/02_raw_pixels has no dependencies at all.

find_package(glfw3 QUIET)

set(GLFW_LIBS "")
set(GLFW_INCLUDE "")
if(glfw3_FOUND)
  set(GLFW_LIBS glfw)
else()
  message(STATUS "GLFW not found — building only dependency-free examples")
endif()

if(glfw3_FOUND)
  add_executable(01_hello 01_hello/main.cpp)
  target_link_libraries(01_hello PRIVATE slikui ${GLFW_LIBS})

  add_executable(03_gallery 03_gallery/main.cpp)
  target_link_libraries(03_gallery PRIVATE slikui ${GLFW_LIBS})

  add_executable(04_glfw_opengl 04_glfw_opengl/main.cpp)
  target_link_libraries(04_glfw_opengl PRIVATE slikui ${GLFW_LIBS})

  add_executable(04_glfw_software 04_glfw_software/main.cpp)
  target_link_libraries(04_glfw_software PRIVATE slikui ${GLFW_LIBS})

  add_executable(05_showcase 05_showcase/main.cpp)
  target_link_libraries(05_showcase PRIVATE slikui ${GLFW_LIBS})
endif()

if(APPLE)
  foreach(tgt 01_hello 03_gallery 04_glfw_opengl 04_glfw_software 05_showcase)
    if(TARGET ${tgt})
      target_link_libraries(${tgt} PRIVATE "-framework OpenGL")
    endif()
  endforeach()
elseif(UNIX)
  find_package(OpenGL REQUIRED)
  foreach(tgt 01_hello 03_gallery 04_glfw_opengl 04_glfw_software 05_showcase)
    if(TARGET ${tgt})
      target_link_libraries(${tgt} PRIVATE OpenGL::GL)
    endif()
  endforeach()
endif()

# Dependency-free headless example — always builds.
add_executable(02_raw_pixels 02_raw_pixels/main.cpp)
target_link_libraries(02_raw_pixels PRIVATE slikui)
