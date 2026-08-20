# apps/filebrowser - read-only navigation of the littlefs volume.

add_library(cads_app_filebrowser STATIC
    cads_filebrowser.c
)

target_include_directories(cads_app_filebrowser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cads_app_filebrowser PUBLIC cads_gui_view cads_toolbox cads_storage PRIVATE cads_flags)
