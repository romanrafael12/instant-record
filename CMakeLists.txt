cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# --- macOS frontend header (vendored) ---------------------------------------
# On macOS the OBS 31.x sources are compiled with the frontend disabled, so the
# build produces the obs-frontend-api library but never installs its public
# header. We ship a vendored copy of the real OBS 31.1.1 header and put it on
# the global include path so every source file finds <obs-frontend-api.h>.
# macOS-only; on Windows the prebuilt OBS already ships the header.
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

# --- Frontend API -----------------------------------------------------------
# Link the frontend-api library when its find module is available. It is NOT
# fatal if it isn't: on macOS the obs_frontend_* symbols (including the dock
# registration) are resolved from OBS itself at load time via the
# dynamic_lookup linker option below.
find_package(obs-frontend-api QUIET)
if(TARGET OBS::obs-frontend-api)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()
if(APPLE)
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/obs-frontend")
endif()

# Always define ENABLE_FRONTEND_API. This plugin unconditionally uses the
# frontend API (dock, global hotkeys, scene queries). On macOS the CMake option
# can evaluate false under the OBS template, which would compile the empty dock
# stub so the dock never registers. Defining it unconditionally guarantees the
# real dock code is built on every platform. (Windows already had it, so this
# changes nothing there.)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
# ---------------------------------------------------------------------------

# --- Qt (always) ------------------------------------------------------------
# The dock is a Qt widget, so Qt must be found, moc'd, and linked on every
# platform. Kept outside any ENABLE_QT guard for the same reason as above.
find_package(Qt6 COMPONENTS Widgets Core)
if(TARGET Qt6::Widgets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
endif()
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)
# ---------------------------------------------------------------------------

# --- macOS: resolve frontend symbols at load time ---------------------------
# obs_frontend_* functions are provided by OBS when it loads the plugin. In
# headless CI there is no linkable frontend-api library, so leave those symbols
# undefined and resolve them dynamically at load. Standard for OBS plugins on
# macOS; Windows is unaffected.
if(APPLE)
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,-undefined,dynamic_lookup)
endif()
# ---------------------------------------------------------------------------

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/source-record-filter.c src/source-record-dock.cpp src/clean-program.c)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
