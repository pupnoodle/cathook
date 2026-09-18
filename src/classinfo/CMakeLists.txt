set(files "${CMAKE_CURRENT_LIST_DIR}/classinfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dump.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dynamic.gen.cpp")
target_sources(cathook PRIVATE ${files})
set_source_files_properties(${files} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
