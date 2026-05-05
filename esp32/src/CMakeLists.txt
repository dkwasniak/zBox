FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
list(FILTER app_sources EXCLUDE REGEX ".*/sync_mode\\.cpp$")

idf_component_register(SRCS ${app_sources})
