# CaDS Zero - config module. Portable like modules/storage: the parse/serialize
# half is pure C over caller buffers, load/save wrap cads/storage, and both
# implementations link the same object file on host and board.

add_library(cads_config STATIC
    src/cads_config.c
)

target_include_directories(cads_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(cads_config PUBLIC cads_toolbox cads_storage PRIVATE cads_flags)
