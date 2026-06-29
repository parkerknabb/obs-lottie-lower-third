include(ExternalProject)

set(THORVG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg")
set(THORVG_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/thorvg-build")
set(THORVG_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/thorvg-install")

if(NOT EXISTS "${THORVG_SOURCE_DIR}/meson.build")
  message(FATAL_ERROR "Bundled ThorVG source not found. Run: git submodule update --init --recursive")
endif()

find_program(MESON_EXECUTABLE meson REQUIRED)
find_program(NINJA_EXECUTABLE ninja REQUIRED)

if(WIN32)
  set(THORVG_LIBRARY "${THORVG_INSTALL_DIR}/lib/thorvg-1.lib")
else()
  set(THORVG_LIBRARY "${THORVG_INSTALL_DIR}/lib/libthorvg-1.a")
endif()

set(THORVG_MESON_BUILD_TYPE "release")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(THORVG_MESON_BUILD_TYPE "debug")
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  set(THORVG_MESON_BUILD_TYPE "debugoptimized")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(THORVG_MESON_BUILD_TYPE "release")
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(THORVG_MESON_BUILD_TYPE "minsize")
elseif(CMAKE_BUILD_TYPE)
  message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE for bundled ThorVG: ${CMAKE_BUILD_TYPE}")
endif()

set(THORVG_MESON_SETUP_ARGS)
if(WIN32)
  list(APPEND THORVG_MESON_SETUP_ARGS --vsenv)
endif()

function(thorvg_make_meson_array output)
  set(result "[")
  set(separator "")

  foreach(value IN LISTS ARGN)
    string(REPLACE "\\" "\\\\" escaped "${value}")
    string(REPLACE "'" "\\'" escaped "${escaped}")
    string(APPEND result "${separator}'${escaped}'")
    set(separator ", ")
  endforeach()

  string(APPEND result "]")
  set("${output}" "${result}" PARENT_SCOPE)
endfunction()

if(APPLE)
  set(THORVG_MACOS_COMPILE_ARGS)

  foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    list(APPEND THORVG_MACOS_COMPILE_ARGS -arch "${arch}")
  endforeach()

  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND THORVG_MACOS_COMPILE_ARGS "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()

  if(CMAKE_OSX_SYSROOT AND IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    list(APPEND THORVG_MACOS_COMPILE_ARGS -isysroot "${CMAKE_OSX_SYSROOT}")
  endif()

  if(THORVG_MACOS_COMPILE_ARGS)
    thorvg_make_meson_array(THORVG_MACOS_MESON_ARGS ${THORVG_MACOS_COMPILE_ARGS})

    set(THORVG_MESON_NATIVE_FILE "${CMAKE_CURRENT_BINARY_DIR}/third_party/thorvg-macos.ini")
    file(
      WRITE "${THORVG_MESON_NATIVE_FILE}"
      "[built-in options]\n"
      "cpp_args = ${THORVG_MACOS_MESON_ARGS}\n"
      "cpp_link_args = ${THORVG_MACOS_MESON_ARGS}\n"
    )

    list(APPEND THORVG_MESON_SETUP_ARGS --native-file "${THORVG_MESON_NATIVE_FILE}")
  endif()
endif()

ExternalProject_Add(
  thorvg_external
  SOURCE_DIR "${THORVG_SOURCE_DIR}"
  BINARY_DIR "${THORVG_BUILD_DIR}"
  INSTALL_DIR "${THORVG_INSTALL_DIR}"
  CONFIGURE_COMMAND
    "${MESON_EXECUTABLE}" setup "${THORVG_BUILD_DIR}" "${THORVG_SOURCE_DIR}" ${THORVG_MESON_SETUP_ARGS} --prefix
    "${THORVG_INSTALL_DIR}" --libdir lib --backend ninja --buildtype "${THORVG_MESON_BUILD_TYPE}" --default-library
    static -Dengines=cpu -Dloaders=lottie,png,jpg,ttf,otf -Dbindings=capi -Dsavers= -Dtools= -Dtests=false -Dstatic=true
    -Dextra=lottie_exp
  BUILD_COMMAND "${MESON_EXECUTABLE}" compile -C "${THORVG_BUILD_DIR}"
  INSTALL_COMMAND "${MESON_EXECUTABLE}" install -C "${THORVG_BUILD_DIR}"
  BUILD_BYPRODUCTS "${THORVG_LIBRARY}"
)

file(MAKE_DIRECTORY "${THORVG_INSTALL_DIR}/include/thorvg-1")

add_library(ThorVG::ThorVG STATIC IMPORTED GLOBAL)
add_dependencies(ThorVG::ThorVG thorvg_external)

set_target_properties(
  ThorVG::ThorVG
  PROPERTIES
    IMPORTED_LOCATION "${THORVG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${THORVG_INSTALL_DIR}/include/thorvg-1"
)
