if(WIN32)
  find_package(graphviz CONFIG REQUIRED)
  find_package(ZLIB REQUIRED)
  if(NOT PKG_CONFIG_EXECUTABLE)
    file(GLOB _pkg_config_candidates
      "${_VCPKG_INSTALLED_DIR}/*/tools/pkgconf/pkgconf.exe"
    )
    list(GET _pkg_config_candidates 0 _pkg_config_executable)
    set(PKG_CONFIG_EXECUTABLE "${_pkg_config_executable}"
      CACHE FILEPATH "pkg-config executable" FORCE)
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo pangoft2)

  function(add_graphviz_static_import target library_name)
    find_library("${target}_DEBUG"
      NAMES "${library_name}"
      PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
      NO_DEFAULT_PATH
      REQUIRED
    )
    find_library("${target}_RELEASE"
      NAMES "${library_name}"
      PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
      NO_DEFAULT_PATH
      REQUIRED
    )
    add_library("${target}" STATIC IMPORTED GLOBAL)
    set_target_properties("${target}" PROPERTIES
      IMPORTED_LOCATION_DEBUG "${${target}_DEBUG}"
      IMPORTED_LOCATION_RELEASE "${${target}_RELEASE}"
      IMPORTED_LOCATION_MINSIZEREL "${${target}_RELEASE}"
      IMPORTED_LOCATION_RELWITHDEBINFO "${${target}_RELEASE}"
    )
  endfunction()

  add_graphviz_static_import(graphviz_plugin_dot_layout gvplugin_dot_layout)
  add_graphviz_static_import(graphviz_plugin_pango gvplugin_pango)
  add_graphviz_static_import(graphviz_dotgen dotgen)

  set(GRAPHVIZ_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
  file(MAKE_DIRECTORY
    "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/cgraph"
    "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/common"
    "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/gvc"
  )
  file(WRITE "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/cgraph/cgraph.h"
    "#include <graphviz/cgraph.h>\n")
  file(WRITE "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/common/types.h"
    "#include <graphviz/types.h>\n")
  file(WRITE "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/gvc/gvc.h"
    "#include <graphviz/gvc.h>\n")
  file(WRITE "${GRAPHVIZ_COMPAT_INCLUDE_DIR}/gvc/gvplugin.h"
    "#include <graphviz/gvplugin.h>\n")

  add_library(graphviz_dot_static INTERFACE)
  target_include_directories(graphviz_dot_static SYSTEM INTERFACE
    "${GRAPHVIZ_COMPAT_INCLUDE_DIR}"
    "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
  )
  target_link_libraries(graphviz_dot_static INTERFACE
    graphviz::gvc
    graphviz::cgraph
    graphviz::cdt
    graphviz::pathplan
    graphviz::xdot
    graphviz::private::common
    graphviz::private::util
    graphviz_plugin_dot_layout
    graphviz_plugin_pango
    graphviz_dotgen
    PkgConfig::PANGOCAIRO
    ZLIB::ZLIB
  )
  return()
endif()

include(ExternalProject)
include(FetchContent)
find_package(EXPAT REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
find_program(GRAPHVIZ_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo pangoft2)

FetchContent_Declare(graphviz
  URL https://gitlab.com/api/v4/projects/4207231/packages/generic/graphviz-releases/16.1.0/graphviz-16.1.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(graphviz)
if(NOT graphviz_POPULATED)
  FetchContent_Populate(graphviz)
endif()

set(GRAPHVIZ_SOURCE_DIR "${graphviz_SOURCE_DIR}")
set(GRAPHVIZ_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/graphviz-build")

ExternalProject_Add(graphviz_static_build
  SOURCE_DIR "${GRAPHVIZ_SOURCE_DIR}"
  BINARY_DIR "${GRAPHVIZ_BUILD_DIR}"
  CONFIGURE_COMMAND
    "${GRAPHVIZ_SOURCE_DIR}/configure"
    "--prefix=<INSTALL_DIR>"
    --disable-shared
    --enable-static
    --disable-ltdl
    --disable-swig
    --disable-tcl
    --with-pangocairo=yes
    --without-libgd
    --without-webp
    --without-quartz
    --without-lasi
    --without-devil
    --without-poppler
    --without-rsvg
    --without-gts
  BUILD_COMMAND
    "${GRAPHVIZ_MAKE_EXECUTABLE}" -C lib
    COMMAND
    "${GRAPHVIZ_MAKE_EXECUTABLE}" -C plugin/dot_layout libgvplugin_dot_layout_C.la
    COMMAND
    "${GRAPHVIZ_MAKE_EXECUTABLE}" -C plugin/pango libgvplugin_pango_C.la
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    "${GRAPHVIZ_BUILD_DIR}/plugin/dot_layout/.libs/libgvplugin_dot_layout_C.a"
    "${GRAPHVIZ_BUILD_DIR}/plugin/pango/.libs/libgvplugin_pango_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/dotgen/.libs/libdotgen_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/gvc/.libs/libgvc_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/pack/.libs/libpack_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/common/.libs/libcommon_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/label/.libs/liblabel_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/ortho/.libs/libortho_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/xdot/.libs/libxdot_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/pathplan/.libs/libpathplan_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/cgraph/.libs/libcgraph_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/cdt/.libs/libcdt_C.a"
    "${GRAPHVIZ_BUILD_DIR}/lib/util/.libs/libutil_C.a"
)

set(_graphviz_static_archives
  plugin_dot_layout:plugin/dot_layout/.libs/libgvplugin_dot_layout_C.a
  plugin_pango:plugin/pango/.libs/libgvplugin_pango_C.a
  dotgen:lib/dotgen/.libs/libdotgen_C.a
  gvc:lib/gvc/.libs/libgvc_C.a
  pack:lib/pack/.libs/libpack_C.a
  common:lib/common/.libs/libcommon_C.a
  label:lib/label/.libs/liblabel_C.a
  ortho:lib/ortho/.libs/libortho_C.a
  xdot:lib/xdot/.libs/libxdot_C.a
  pathplan:lib/pathplan/.libs/libpathplan_C.a
  cgraph:lib/cgraph/.libs/libcgraph_C.a
  cdt:lib/cdt/.libs/libcdt_C.a
  util:lib/util/.libs/libutil_C.a
)

set(_graphviz_link_targets)
foreach(_entry IN LISTS _graphviz_static_archives)
  string(REPLACE ":" ";" _parts "${_entry}")
  list(GET _parts 0 _name)
  list(GET _parts 1 _path)
  set(_target "graphviz_${_name}")
  add_library("${_target}" STATIC IMPORTED GLOBAL)
  set_target_properties("${_target}" PROPERTIES
    IMPORTED_LOCATION "${GRAPHVIZ_BUILD_DIR}/${_path}"
  )
  list(APPEND _graphviz_link_targets "${_target}")
endforeach()

string(JOIN "," _graphviz_rescan_targets ${_graphviz_link_targets})
add_library(graphviz_dot_static INTERFACE)
target_include_directories(graphviz_dot_static SYSTEM INTERFACE
  "${GRAPHVIZ_BUILD_DIR}"
  "${GRAPHVIZ_SOURCE_DIR}/lib"
  "${GRAPHVIZ_SOURCE_DIR}/lib/common"
  "${GRAPHVIZ_SOURCE_DIR}/lib/gvc"
  "${GRAPHVIZ_SOURCE_DIR}/lib/cgraph"
  "${GRAPHVIZ_SOURCE_DIR}/lib/cdt"
  "${GRAPHVIZ_SOURCE_DIR}/lib/pathplan"
)
target_link_libraries(graphviz_dot_static INTERFACE
  "$<LINK_GROUP:RESCAN,${_graphviz_rescan_targets}>"
  PkgConfig::PANGOCAIRO
  EXPAT::EXPAT
  ZLIB::ZLIB
  m
)
