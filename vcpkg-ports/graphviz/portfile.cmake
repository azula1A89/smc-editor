include("${VCPKG_ROOT_DIR}/ports/graphviz/portfile.cmake")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
  file(INSTALL
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/lib/dotgen/dotgen.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
  )
  if(NOT VCPKG_BUILD_TYPE)
    file(INSTALL
      "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/lib/dotgen/dotgen.lib"
      DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib"
    )
  endif()
endif()
