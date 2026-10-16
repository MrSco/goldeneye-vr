cmake_minimum_required(VERSION 3.16)
project(gevr_ragdoll_test C CXX)
get_filename_component(GEVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory("${GEVR_ROOT}/port/external/pdphysics" physics)
add_executable(ragdoll_native "${GEVR_TEST_SOURCE}"
  "${GEVR_ROOT}/port/src/gevr_ragdoll_mesh.c"
  "${GEVR_ROOT}/port/src/gevr_ragdoll_world.c")
target_include_directories(ragdoll_native PRIVATE
  "${GEVR_ROOT}" "${GEVR_ROOT}/include" "${GEVR_ROOT}/src"
  "${GEVR_ROOT}/src/game" "${GEVR_ROOT}/port/include" "${GEVR_ROOT}/port/src")
target_compile_definitions(ragdoll_native PRIVATE GEVR=1 PLATFORM_64BIT=1 _LANGUAGE_C=1 VERSION_US=1)
target_compile_options(ragdoll_native PRIVATE -fms-extensions -Wno-incompatible-pointer-types
  -Wno-builtin-declaration-mismatch -include stddef.h -g -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(ragdoll_native PRIVATE -fsanitize=address,undefined)
target_link_libraries(ragdoll_native PRIVATE gevr_physics m pthread)
