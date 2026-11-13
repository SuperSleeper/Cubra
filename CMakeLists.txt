cmake_minimum_required(VERSION 3.21)
project(Cubra VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL OpenGLWidgets Test)

# Manifold is the geometry kernel: its booleans always yield watertight meshes.
include(FetchContent)
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(manifold
  GIT_REPOSITORY https://github.com/elalish/manifold.git
  GIT_TAG v3.5.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(manifold)

add_library(td_core STATIC
  src/core/Feature.cpp
  src/core/Document.cpp
  src/core/Pattern.cpp
  src/core/Geometry.cpp
  src/core/Picking.cpp
  src/core/Placement.cpp
  src/core/Topology.cpp
  src/core/Modify.cpp
  src/core/Exporters.cpp)
target_include_directories(td_core PUBLIC src)
target_link_libraries(td_core PUBLIC Qt6::Core PRIVATE manifold::manifold)

add_library(td_ai STATIC
  src/ai/GpuProbe.cpp
  src/ai/ModelCatalog.cpp
  src/ai/CadPrompt.cpp
  src/ai/OllamaClient.cpp
  src/ai/DesignSession.cpp)
target_link_libraries(td_ai PUBLIC td_core Qt6::Network)

add_library(td_app STATIC
  src/app/MainWindow.cpp
  src/app/Viewport.cpp
  src/app/FeaturePanel.cpp
  src/app/AiPanel.cpp
  src/app/PlacementController.cpp
  src/app/HoverMenu.cpp
  src/app/StepTool.cpp
  src/app/TransformController.cpp
  src/app/PatternController.cpp
  src/app/SnapSettings.cpp
  src/app/ViewCube.cpp
  src/app/ViewportChrome.cpp
  src/app/SelectionController.cpp
  src/app/ModifyController.cpp
  src/app/Icons.cpp)
target_link_libraries(td_app PUBLIC td_ai Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

add_executable(cubra src/app/main.cpp)
set_target_properties(cubra PROPERTIES OUTPUT_NAME cubra)
target_link_libraries(cubra PRIVATE td_app)
qt_add_resources(cubra "appicon" PREFIX "/" BASE packaging FILES packaging/cubra.png)

# Puts Cubra in the desktop's app list, with its icon: cmake --build build --target desktop-entry
configure_file(packaging/cubra.desktop.in ${CMAKE_BINARY_DIR}/cubra.desktop @ONLY)
add_custom_target(desktop-entry
  COMMAND ${CMAKE_COMMAND} -E make_directory $ENV{HOME}/.local/share/applications $ENV{HOME}/.local/share/icons/hicolor/512x512/apps
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/packaging/cubra.png $ENV{HOME}/.local/share/icons/hicolor/512x512/apps/cubra.png
  # The old vector icon would win over the PNG, so remove it.
  COMMAND ${CMAKE_COMMAND} -E rm -f $ENV{HOME}/.local/share/icons/hicolor/scalable/apps/cubra.svg
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cubra.desktop $ENV{HOME}/.local/share/applications/cubra.desktop
  DEPENDS cubra
  COMMENT "Installing the Cubra launcher and icon for this user")

# Headless evaluate/export tool, also used by the tests and the model evaluation.
add_executable(cubra_cli tools/cli.cpp)
set_target_properties(cubra_cli PROPERTIES OUTPUT_NAME cubra-cli)
target_link_libraries(cubra_cli PRIVATE td_ai)

enable_testing()
add_executable(td_tests tests/test_core.cpp)
target_link_libraries(td_tests PRIVATE td_ai)
add_test(NAME core COMMAND td_tests)

add_executable(td_gui_tests tests/test_gui.cpp)
target_link_libraries(td_gui_tests PRIVATE td_app Qt6::Test)
add_test(NAME gui COMMAND td_gui_tests)
set_tests_properties(gui PROPERTIES SKIP_RETURN_CODE 77)

install(TARGETS cubra cubra_cli RUNTIME DESTINATION bin)

# A throwaway program for investigating geometry: cmake -DTD_PROBE=/path/probe.cpp
if(TD_PROBE)
  add_executable(td_probe ${TD_PROBE})
  target_include_directories(td_probe PRIVATE src)
  target_link_libraries(td_probe PRIVATE td_ai)
endif()
