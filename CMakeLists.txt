cmake_minimum_required(VERSION 3.16)
project(groundwork CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# Sound effects via the vendored miniaudio header (no system package). OFF
# builds the same game with silent audio stubs.
option(ENABLE_AUDIO "Build with sound effects (miniaudio)" ON)

add_executable(groundwork
  src/main.cpp
  src/Chunk.cpp
  src/World.cpp
  src/Player.cpp
  src/Physics.cpp
  src/Entity.cpp
  src/ItemRenderer.cpp
  src/Texture.cpp
  src/Hud.cpp
  src/Terrain.cpp
  src/Audio.cpp
)

target_compile_definitions(groundwork PRIVATE GL_GLEXT_PROTOTYPES)
target_include_directories(groundwork PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(groundwork PRIVATE ${GLFW_LIBRARIES} GL Threads::Threads)
target_compile_options(groundwork PRIVATE -Wall -O2)
if(ENABLE_AUDIO)
  target_compile_definitions(groundwork PRIVATE AUDIO_ENABLED)
  # SYSTEM: miniaudio is not ours to keep -Wall-clean.
  target_include_directories(groundwork SYSTEM PRIVATE third_party)
  target_link_libraries(groundwork PRIVATE ${CMAKE_DL_LIBS} m)
endif()

# Release packaging: `cmake --install build --strip --prefix dist` yields a
# stripped, self-contained binary (all assets are generated at startup).
install(TARGETS groundwork RUNTIME DESTINATION bin)

enable_testing()
add_executable(world_tests
  tests/test_world.cpp
  src/Chunk.cpp
  src/World.cpp
  src/Terrain.cpp
  src/Player.cpp
  src/Physics.cpp
  src/Entity.cpp
)
target_compile_definitions(world_tests PRIVATE GL_GLEXT_PROTOTYPES)
target_link_libraries(world_tests PRIVATE GL Threads::Threads)
target_compile_options(world_tests PRIVATE -Wall -O2)
add_test(NAME world_tests COMMAND world_tests)

# Golden-screenshot regression test: needs a display; skips cleanly without one.
add_test(NAME golden_screenshot
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_screenshot.py
          $<TARGET_FILE:groundwork>
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/reference.png)
set_tests_properties(golden_screenshot PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
