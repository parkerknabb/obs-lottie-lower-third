cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
option(USE_BUNDLED_THORVG "Build the bundled ThorVG third-party source" ON)

include(compilerconfig)
include(defaults)
include(helpers)

if(USE_BUNDLED_THORVG)
  message(STATUS "Using bundled ThorVG from deps/thorvg")
  include(cmake/ThorVGVendored.cmake)
else()
  message(STATUS "Using system/package ThorVG")
  if(APPLE OR UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(THORVG REQUIRED IMPORTED_TARGET thorvg-1)
    add_library(ThorVG::ThorVG ALIAS PkgConfig::THORVG)
  elseif(WIN32)
    find_package(unofficial-thorvg CONFIG QUIET)
    if(TARGET unofficial::thorvg::thorvg)
      add_library(ThorVG::ThorVG ALIAS unofficial::thorvg::thorvg)
    else()
      find_path(THORVG_INCLUDE_DIR thorvg_capi.h)
      find_library(THORVG_LIB NAMES thorvg-1 thorvg)
      if(THORVG_INCLUDE_DIR AND THORVG_LIB)
        add_library(ThorVG::ThorVG UNKNOWN IMPORTED)
        set_target_properties(
          ThorVG::ThorVG
          PROPERTIES IMPORTED_LOCATION "${THORVG_LIB}" INTERFACE_INCLUDE_DIRECTORIES "${THORVG_INCLUDE_DIR}"
        )
      else()
        message(FATAL_ERROR "ThorVG not found. Enable USE_BUNDLED_THORVG or install ThorVG.")
      endif()
    endif()
  endif()
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/test-source.c deps/cJSON/cJSON.c)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE deps/cJSON)
if(USE_BUNDLED_THORVG)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TVG_STATIC=1)
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
  find_package(Threads REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Threads::Threads)
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ThorVG::ThorVG)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
