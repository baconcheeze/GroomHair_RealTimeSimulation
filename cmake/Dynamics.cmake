set(_dynamics_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/dynamics")
set(_dynamics_out "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders/dynamics")
set(_dynamics_outputs)
foreach(_entry BeginSubstep EndSubstep KickVelocity)
  add_custom_command(OUTPUT "${_dynamics_out}/${_entry}.dxil"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dynamics_out}"
    COMMAND "${HAIR_DXC}" ${_compile_flags} -E ${_entry} -Fo "${_dynamics_out}/${_entry}.dxil" "${_dynamics_dir}/Dynamics.hlsl"
    DEPENDS "${HAIR_DXC}" "${_dxc_directory}/dxcompiler.dll" "${_dynamics_dir}/Dynamics.hlsl"
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders/orientation/GuideQuaternion.hlsl"
    COMMAND_EXPAND_LISTS VERBATIM)
  list(APPEND _dynamics_outputs "${_dynamics_out}/${_entry}.dxil")
endforeach()
add_custom_target(HairDynamicsShaders DEPENDS ${_dynamics_outputs})
add_dependencies(HairLabDX12 HairDynamicsShaders)
file(GENERATE OUTPUT "${_dynamics_out}/compiler.txt" CONTENT
"DXC=${HAIR_DXC}\nVersion=${_dxc_version}\nProfile=cs_6_0\nFlags=-HV 2021 -Gis $<IF:$<CONFIG:Debug>,-Od -Zi -Qembed_debug,-O3>\nCompiler hashes: ../orientation/compiler.txt\n")
