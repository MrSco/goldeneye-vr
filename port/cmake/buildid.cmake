# Writes the build id (git commit, whether the tree had uncommitted changes, and
# the build time) into a one-line C file, run on every build by the gevr_buildid
# target in CMakeLists.txt. Shown on the in-VR launcher (port/vr/vr_launcher.cpp)
# so the build on the headset can be told at a glance.
#   cmake -DSRC_DIR=<repo> -DOUT=<file.c> -P buildid.cmake
find_package(Git QUIET)
set(HASH "nogit")
set(DIRTY "")
set(BRANCH "")
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                  WORKING_DIRECTORY "${SRC_DIR}" OUTPUT_VARIABLE HASH
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    set(HASH "nogit")
  endif()
  # The branch too, per build: CMakeLists.txt's VERSION_BRANCH is fixed at
  # configure time, which is how a v0.3.7 release logged a feature branch.
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
                  WORKING_DIRECTORY "${SRC_DIR}" OUTPUT_VARIABLE BRANCH
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                  WORKING_DIRECTORY "${SRC_DIR}" OUTPUT_VARIABLE st
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT "${st}" STREQUAL "")
    set(DIRTY "+")
  endif()
endif()
if(BRANCH STREQUAL "" OR BRANCH STREQUAL "HEAD")
  set(BRANCH "main")
endif()
string(TIMESTAMP WHEN "%Y-%m-%d %H:%M")
file(WRITE "${OUT}.tmp" "const char gevrBuildId[] = \"${HASH}${DIRTY}  built ${WHEN}\";\nconst char gevrBuildBranch[] = \"${BRANCH}\";\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT}.tmp" "${OUT}")
