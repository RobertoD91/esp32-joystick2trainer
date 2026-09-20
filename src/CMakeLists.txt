# Main application component. The hardware independent libraries live in lib/ and are
# compiled by PlatformIO's library dependency finder.
file(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
)
