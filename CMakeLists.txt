cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# --- macOS frontend header (vendored) ---------------------------------------
# On macOS the OBS 31.x sources are compiled with the frontend disabled, so the
# build produces the obs-frontend-api library but never installs its public
# header. We ship a vendored copy of the real OBS 31.1.1 header and put it on
# the global include path so every source file finds <obs-frontend-api.h>.
# This is macOS-only: on Windows the prebuilt OBS already ships the header, so
# this block does nothing and the Windows build is unchanged.
if(APPLE)
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/vendor/obs-frontend")
endif()
# ---------------------------------------------------------------------------

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  # Belt-and-suspenders: also add the vendored header dir to this target on
  # macOS, in case include_directories() above is scoped away by the template.
  if(APPLE)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/obs-frontend")
  endif()
endif()

# --- macOS frontend symbols resolved at load time ---------------------------
# The obs_frontend_* functions are provided by OBS itself when it loads the
# plugin. In headless CI there is no linkable frontend-api library, so tell the
# linker to leave those symbols undefined and resolve them dynamically at load.
# This is the standard approach for OBS plugins on macOS; Windows is unaffected.
if(APPLE)
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,-undefined,dynamic_lookup)
endif()
# ---------------------------------------------------------------------------

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

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/source-record-filter.c src/source-record-dock.cpp src/clean-program.c)

if(ENABLE_FRONTEND_API)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
