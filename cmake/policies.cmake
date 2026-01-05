function(smol_set_rpath_policy TARGET_NAME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR LINUX)
        message(STATUS "Applying RPATH policy to target: ${TARGET_NAME}")

        set_target_properties(
            ${TARGET_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
            SKIP_BUILD_RPATH FALSE
        )

        target_link_options(
            ${TARGET_NAME} PRIVATE
            "LINKER:--disable-new-dtags"
        )
    endif()
endfunction()