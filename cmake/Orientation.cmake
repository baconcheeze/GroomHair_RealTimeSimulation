# Use an installed x64 DXC. No download, global PATH change, or runtime compiler.
file(GLOB _dxc_paths "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
list(SORT _dxc_paths COMPARE NATURAL ORDER DESCENDING)
find_program(HAIR_DXC NAMES dxc.exe HINTS ${_dxc_paths} REQUIRED)
execute_process(COMMAND "${HAIR_DXC}" --version OUTPUT_VARIABLE _dxc_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Hair orientation DXC: ${HAIR_DXC}: ${_dxc_version}")
file(SHA256 "${HAIR_DXC}" _dxc_hash)
get_filename_component(_dxc_directory "${HAIR_DXC}" DIRECTORY)
file(SHA256 "${_dxc_directory}/dxcompiler.dll" _dxc_dll_hash)
set(_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/orientation")
set(_compiled_dir "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders/orientation")
set(_compile_flags -T cs_6_0 -HV 2021 -Gis "$<IF:$<CONFIG:Debug>,-Od,-O3>" "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Qembed_debug>")
set(_outputs)
set(_source_hashes "")
foreach(_source GuideSolve.hlsl LocalValidation.hlsl GuideQuaternion.hlsl GuideOrientationLocal.hlsl)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_shader_dir}/${_source}")
  file(SHA256 "${_shader_dir}/${_source}" _hash)
  string(APPEND _source_hashes "${_source} SHA256=${_hash}\n")
endforeach()
foreach(_shader GuideSolve LocalValidation)
  if(_shader STREQUAL "GuideSolve")
    set(_entry SolveOrientation)
  else()
    set(_entry RobustLocal)
  endif()
  add_custom_command(OUTPUT "${_compiled_dir}/${_shader}.dxil"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_compiled_dir}"
    COMMAND "${HAIR_DXC}" ${_compile_flags} -E ${_entry} -I "${_shader_dir}" -Fo "${_compiled_dir}/${_shader}.dxil" "${_shader_dir}/${_shader}.hlsl"
    DEPENDS "${HAIR_DXC}" "${_dxc_directory}/dxcompiler.dll" "${_shader_dir}/${_shader}.hlsl" "${_shader_dir}/GuideQuaternion.hlsl" "${_shader_dir}/GuideOrientationLocal.hlsl"
    COMMAND_EXPAND_LISTS VERBATIM)
  list(APPEND _outputs "${_compiled_dir}/${_shader}.dxil")
endforeach()
add_custom_target(HairOrientationShaders DEPENDS ${_outputs}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/references/orientation/StableCosserat-CPU-LICENSE.txt"
    "${_compiled_dir}/StableCosserat-CPU-LICENSE.txt")
add_dependencies(HairLabDX12 HairOrientationShaders)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/orientation-compiler-$<CONFIG>.txt" CONTENT
"DXC=${HAIR_DXC}\nVersion=${_dxc_version}\nCompilerSHA256=${_dxc_hash}\ndxcompiler.dll SHA256=${_dxc_dll_hash}\nProfile=cs_6_0\nFlags=-HV 2021 -Gis $<IF:$<CONFIG:Debug>,-Od -Zi -Qembed_debug,-O3>\n${_source_hashes}")
file(GENERATE OUTPUT "${_compiled_dir}/compiler.txt" CONTENT
"DXC=${HAIR_DXC}\nVersion=${_dxc_version}\nCompilerSHA256=${_dxc_hash}\ndxcompiler.dll SHA256=${_dxc_dll_hash}\nProfile=cs_6_0\nFlags=-HV 2021 -Gis $<IF:$<CONFIG:Debug>,-Od -Zi -Qembed_debug,-O3>\n${_source_hashes}")
