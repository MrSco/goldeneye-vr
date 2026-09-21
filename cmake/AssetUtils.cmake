# execute a header generator (execcmd) for every json file in jsonpath, collect headers in headerlist
# note that this reads ROMID
macro(generate_asset_headers jsonpath execcmd extraarg headerlist)
  set(TMP_JSON "")

  if (${jsonpath} MATCHES "\.json")
    # is a json file
    list(APPEND TMP_JSON "${CMAKE_SOURCE_DIR}/${ASSET_DIR}/${jsonpath}")
  else()
    # is a directory
    file(GLOB TMP_JSON "${CMAKE_SOURCE_DIR}/${ASSET_DIR}/${jsonpath}*.json")
  endif()

  foreach(JSON ${TMP_JSON})
    unset(HEADERNAME)
    string(REPLACE ".json" ".h" HEADERNAME ${JSON})
    string(REPLACE "${CMAKE_SOURCE_DIR}/${ASSET_DIR}" "${CMAKE_BINARY_DIR}/${GENERATED_DIR}" HEADERNAME ${HEADERNAME})
    if(WIN32 OR ANDROID)
      # On Windows and Android, run Python scripts with python
      add_custom_command(
        OUTPUT  ${HEADERNAME}
        DEPENDS ${JSON}
        COMMAND python ${execcmd} ${JSON} ${extraarg} --headers-only --romid=${ROMID}
      )
    else()
      add_custom_command(
        OUTPUT  ${HEADERNAME}
        DEPENDS ${JSON}
        COMMAND ${execcmd} ${JSON} ${extraarg} --headers-only --romid=${ROMID}
      )
    endif()
    list(APPEND ${headerlist} "${HEADERNAME}")
  endforeach()

  unset(TMP_JSON)
endmacro()
