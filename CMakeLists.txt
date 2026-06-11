cmake_minimum_required(VERSION 3.16)
project(groundwork CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

if(WIN32)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(glm CONFIG REQUIRED)
  if(TARGET glfw)
    set(GROUNDWORK_GLFW_TARGET glfw)
  elseif(TARGET glfw3)
    set(GROUNDWORK_GLFW_TARGET glfw3)
  else()
    message(FATAL_ERROR "glfw3 package found, but no glfw/glfw3 target was exported")
  endif()
  set(GROUNDWORK_GLM_TARGET glm::glm)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
  set(GROUNDWORK_GLFW_TARGET PkgConfig::GLFW)
  set(GROUNDWORK_GLM_TARGET)
endif()

# Sound effects via the vendored miniaudio header (no system package). OFF
# builds the same game with silent audio stubs.
option(ENABLE_AUDIO "Build with sound effects (miniaudio)" ON)

add_executable(groundwork
  src/app/main.cpp
  src/world/Chunk.cpp
  src/world/BlockEntity.cpp
  src/world/Lighting.cpp
  src/world/World.cpp
  src/world/WorldSave.cpp
  src/sim/Player.cpp
  src/sim/Physics.cpp
  src/sim/Mining.cpp
  src/sim/Crafting.cpp
  src/sim/Entity.cpp
  src/render/BreakOverlay.cpp
  src/render/ItemRenderer.cpp
  src/render/GLCompat.cpp
  src/render/Texture.cpp
  src/ui/Hud.cpp
  src/world/Terrain.cpp
  src/audio/Audio.cpp
)

target_link_libraries(groundwork PRIVATE
  ${GROUNDWORK_GLFW_TARGET}
  ${GROUNDWORK_GLM_TARGET}
  OpenGL::GL
  Threads::Threads)
target_include_directories(groundwork PRIVATE src)
if(NOT WIN32)
  target_compile_definitions(groundwork PRIVATE GL_GLEXT_PROTOTYPES)
endif()
if(MSVC)
  target_compile_options(groundwork PRIVATE /W4 /permissive- $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(groundwork PRIVATE -Wall -O2)
endif()
if(ENABLE_AUDIO)
  target_compile_definitions(groundwork PRIVATE AUDIO_ENABLED)
  # SYSTEM: miniaudio is not ours to keep -Wall-clean.
  target_include_directories(groundwork SYSTEM PRIVATE third_party)
  if(NOT WIN32)
    target_link_libraries(groundwork PRIVATE ${CMAKE_DL_LIBS} m)
  endif()
endif()

# Release packaging: `cmake --install build --strip --prefix dist` yields a
# stripped, asset-file-free Linux binary; Windows installs the GLFW runtime
# DLL beside the executable.
install(TARGETS groundwork RUNTIME DESTINATION bin)
if(WIN32)
  install(FILES $<TARGET_FILE:${GROUNDWORK_GLFW_TARGET}> DESTINATION bin)
endif()

enable_testing()
add_executable(world_tests
  tests/test_world.cpp
  src/world/Chunk.cpp
  src/world/BlockEntity.cpp
  src/world/Lighting.cpp
  src/world/World.cpp
  src/world/WorldSave.cpp
  src/world/Terrain.cpp
  src/sim/Player.cpp
  src/sim/Physics.cpp
  src/sim/Mining.cpp
  src/sim/Crafting.cpp
  src/sim/Entity.cpp
  src/render/GLCompat.cpp
)
target_link_libraries(world_tests PRIVATE OpenGL::GL Threads::Threads ${GROUNDWORK_GLM_TARGET})
target_include_directories(world_tests PRIVATE src)
if(NOT WIN32)
  target_compile_definitions(world_tests PRIVATE GL_GLEXT_PROTOTYPES)
endif()
if(MSVC)
  target_compile_options(world_tests PRIVATE /W4 /permissive- $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(world_tests PRIVATE -Wall -O2)
endif()
add_test(NAME world_tests COMMAND world_tests)

# Golden-screenshot regression test: needs Python plus a display; the script
# skips cleanly without a display or PIL.
if(Python3_Interpreter_FOUND)
  add_test(NAME golden_screenshot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_screenshot.py
            $<TARGET_FILE:groundwork>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/reference.png)
  set_tests_properties(golden_screenshot PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
else()
  message(STATUS "Python3 interpreter not found; golden_screenshot test disabled")
endif()
