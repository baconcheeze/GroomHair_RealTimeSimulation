# Reuse the EXISTING compiler, profile, and flags without modifying Orientation.cmake.
set(_window_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/window")
set(_window_out "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders/window")
set(_window_outputs)
set(_window_hashes "")
foreach(_source WindowSolve.hlsl PositionLocal.hlsl PositionValidation.hlsl)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_window_dir}/${_source}")
  file(SHA256 "${_window_dir}/${_source}" _hash)
  string(APPEND _window_hashes "${_source} SHA256=${_hash}\n")
endforeach()
foreach(_entry WindowSolve WindowPerturb PositionValidation)
  if(_entry STREQUAL "PositionValidation")
    set(_file PositionValidation)
    set(_function PositionValidation)
  elseif(_entry STREQUAL "WindowPerturb")
    set(_file WindowSolve)
    set(_function PerturbWindow)
  else()
    set(_file WindowSolve)
    set(_function SolveWindow)
  endif()
  add_custom_command(OUTPUT "${_window_out}/${_entry}.dxil"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_window_out}"
    COMMAND "${HAIR_DXC}" ${_compile_flags} -E ${_function} -I "${_window_dir}" -Fo "${_window_out}/${_entry}.dxil" "${_window_dir}/${_file}.hlsl"
    DEPENDS "${HAIR_DXC}" "${_dxc_directory}/dxcompiler.dll"
      "${_window_dir}/${_file}.hlsl" "${_window_dir}/PositionLocal.hlsl"
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders/orientation/GuideOrientationLocal.hlsl"
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders/orientation/GuideQuaternion.hlsl"
    COMMAND_EXPAND_LISTS VERBATIM)
  list(APPEND _window_outputs "${_window_out}/${_entry}.dxil")
endforeach()
add_custom_target(HairWindowShaders DEPENDS ${_window_outputs})
add_dependencies(HairLabDX12 HairWindowShaders)
file(GENERATE OUTPUT "${_window_out}/compiler.txt" CONTENT
"DXC=${HAIR_DXC}\nVersion=${_dxc_version}\nCompilerSHA256=${_dxc_hash}\ndxcompiler.dll SHA256=${_dxc_dll_hash}\nProfile=cs_6_0\nFlags=-HV 2021 -Gis $<IF:$<CONFIG:Debug>,-Od -Zi -Qembed_debug,-O3>\n${_window_hashes}Robust helper dependency hashes: ../orientation/compiler.txt\n")
