set(_fullsrc "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullstrand")
set(_fulldst "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders/fullstrand")
set(_fulloutputs)
foreach(_entry Begin Advance Factor Initial Correct FinishLinear Export Commit Form MathProbe)
 add_custom_command(OUTPUT "${_fulldst}/${_entry}.dxil"
 COMMAND ${CMAKE_COMMAND} -E make_directory "${_fulldst}"
 COMMAND "${HAIR_DXC}" -T cs_6_0 -HV 2021 -Gis -DGROUP_SIZE=32 "$<IF:$<CONFIG:Debug>,-Od,-O3>" "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Qembed_debug>" -E ${_entry} -Fo "${_fulldst}/${_entry}.dxil" -Fc "${_fulldst}/${_entry}.ll" "${_fullsrc}/FullStrand.hlsl"
 DEPENDS "${_fullsrc}/FullStrand.hlsl" "${_fullsrc}/Linear.hlsli" "${_fullsrc}/DoubleMath.hlsli"
 COMMAND_EXPAND_LISTS VERBATIM)
 list(APPEND _fulloutputs "${_fulldst}/${_entry}.dxil")
endforeach()
add_custom_target(GpuFullStrandShaders DEPENDS ${_fulloutputs})
add_dependencies(HairLabDX12 GpuFullStrandShaders)
target_sources(HairLabDX12 PRIVATE src/GpuFullStrand.cpp)

add_executable(GpuFullStrandTests src/GpuFullStrandTests.cpp src/GpuFullStrand.cpp src/RestData.cpp)
target_link_libraries(GpuFullStrandTests PRIVATE NativeReferenceCore dxguid d3dcompiler)
target_compile_options(GpuFullStrandTests PRIVATE /W4 /permissive- /utf-8 /fp:precise)
set_target_properties(GpuFullStrandTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(GpuFullStrandTests GpuFullStrandShaders HairLabDX12Assets)
