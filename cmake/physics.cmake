# Perfect Dark CE's C interface, isolated from the game's N64 flags/defines.
include(FetchContent)
# Windows' 260-character path limit: FetchContent's stamp files inside a
# worktree's android/app/.cxx/<config>/<hash>/arm64-v8a/_deps exceed it.
if(CMAKE_HOST_WIN32)
  set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/_deps")
endif()
FetchContent_Declare(joltphysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a
  GIT_SHALLOW FALSE)
FetchContent_GetProperties(joltphysics)
if(NOT joltphysics_POPULATED)
  FetchContent_Populate(joltphysics)
endif()
foreach(flag BUILD_SHARED_LIBS JPH_BUILD_SHARED_LIBS ENABLE_ALL_WARNINGS
    INTERPROCEDURAL_OPTIMIZATION GENERATE_DEBUG_SYMBOLS
    FLOATING_POINT_EXCEPTIONS_ENABLED DEBUG_RENDERER_IN_DEBUG_AND_RELEASE
    PROFILER_IN_DEBUG_AND_RELEASE ENABLE_OBJECT_STREAM ENABLE_INSTALL
    JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE
    USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
  set(${flag} OFF)
endforeach()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${joltphysics_SOURCE_DIR}/Build" "${CMAKE_BINARY_DIR}/jolt" EXCLUDE_FROM_ALL)
target_compile_options(Jolt PRIVATE $<$<CONFIG:Debug>:-O2>)
add_library(gevr_physics STATIC "${CMAKE_CURRENT_LIST_DIR}/../port/external/pdphysics/pdphysics.cpp")
target_include_directories(gevr_physics PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../port/external/pdphysics")
target_link_libraries(gevr_physics PRIVATE Jolt)
target_compile_options(gevr_physics PRIVATE $<$<CONFIG:Debug>:-O2>)
set_target_properties(gevr_physics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
