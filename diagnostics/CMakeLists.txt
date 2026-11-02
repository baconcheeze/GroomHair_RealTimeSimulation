cmake_minimum_required(VERSION 3.24)
project(HairLabFirstFailure LANGUAGES CXX)
# Independent build tree. Never edits flags or binaries in the native app build.
set(BUILD_TESTING OFF CACHE BOOL "Diagnostic-only build" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." native)
add_executable(FirstFailure FirstFailure.cpp
  ../src/AutonomousReference.cpp ../src/RestData.cpp)
target_link_libraries(FirstFailure PRIVATE GuideOrientation dxguid)
target_compile_features(FirstFailure PRIVATE cxx_std_20)
target_compile_options(FirstFailure PRIVATE /W4 /permissive- /utf-8 /fp:precise)
set_target_properties(FirstFailure PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(FirstFailure HairDynamicsShaders HairWindowShaders
  HairOrientationShaders HairLabDX12Assets)
