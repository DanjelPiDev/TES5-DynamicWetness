cmake_minimum_required(VERSION 3.21)
message("Using toolchain file ${CMAKE_TOOLCHAIN_FILE}.")

project(DynamicWetness VERSION 1.6.0
        DESCRIPTION "Skyrim Special Edition Dynamic Wetness"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

include(GNUInstallDirs)

set(BUILD_NAME "Release")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  @ONLY)

set(HEADERS
  include/Main.h
  include/WetController.h
  include/Settings.h
  include/UI.h
  include/nlohmann/json.hpp
  include/nlohmann/json_fwd.hpp
  include/IPluginInterface.h
  include/PapyrusAPI.h
  include/utils/Utils.h
)

set(SOURCES
  src/Main.cpp
  src/WetController.cpp
  src/Settings.cpp
  src/UI.cpp
  src/PapyrusAPI.cpp
  src/OverlayMgr.cpp
  src/utils/Utils.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${HEADERS} ${SOURCES})

# ----------------------------------------------------------------------------------------------------------------------
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(ryml CONFIG REQUIRED)
find_path(ARTICUNO_INCLUDE_DIRS "articuno/articuno.h")

# ----------------------------------------------------------------------------------------------------------------------
set(DIRECTXTEX_LIB     "$ENV{DIRECTXTEX_LIB}")
set(DIRECTXTEX_INCLUDE "$ENV{DIRECTXTEX_INCLUDE}")

if (NOT EXISTS "${DIRECTXTEX_LIB}")
  message(FATAL_ERROR "DirectXTex.lib not found under: ${DIRECTXTEX_LIB}")
endif()

add_library(DirectXTex::DirectXTex STATIC IMPORTED GLOBAL)
set_target_properties(DirectXTex::DirectXTex PROPERTIES
  IMPORTED_LOCATION "${DIRECTXTEX_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${DIRECTXTEX_INCLUDE}"
)

# ----------------------------------------------------------------------------------------------------------------------
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${SOURCES})
target_sources(${PROJECT_NAME} PRIVATE ${HEADERS})
add_library("${PROJECT_NAME}::${PROJECT_NAME}" ALIAS "${PROJECT_NAME}")

target_include_directories(${PROJECT_NAME}
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>
    $<INSTALL_INTERFACE:src>
    ${ARTICUNO_INCLUDE_DIRS}
)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ----------------------------------------------------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    ryml::ryml
    DirectXTex::DirectXTex
)

target_compile_definitions(${PROJECT_NAME}
  PRIVATE
    SWE_USE_DIRECTX_TEX
)

if (MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE windowscodecs ole32)
endif()

# ----------------------------------------------------------------------------------------------------------------------
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS ${PROJECT_NAME} DESTINATION "${CMAKE_INSTALL_LIBDIR}")

# BUILD / COPY
install(DIRECTORY DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/Papyrus${BUILD_NAME}/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/Plugin${BUILD_NAME}/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/Plugin${BUILD_NAME}/")

file(GLOB_RECURSE OUTPUT_DLLS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/**/*.dll")
file(GLOB_RECURSE OUTPUT_PDBS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/**/*.pdb")
file(GLOB_RECURSE OUTPUT_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution/**/*.pex")
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${OUTPUT_DLLS}")
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${OUTPUT_PDBS}")
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${OUTPUT_SCRIPTS}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "$ENV{SKYRIM_FOLDER}/Data/SKSE/Plugins/")

if(${CMAKE_BUILD_TYPE} STREQUAL Debug OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> "$ENV{SKYRIM_FOLDER}/Data/SKSE/Plugins/")
endif()
