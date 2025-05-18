function(ADD_DLL_MODULE target_name)
    foreach(dll_name IN LISTS ARGV)
        if(NOT dll_name STREQUAL ${target_name})
            target_include_directories(${target_name} PUBLIC
                ${CMAKE_SOURCE_DIR}/dll/${dll_name}/include
            )
            target_link_libraries(${target_name} PUBLIC ${dll_name})
        endif()
    endforeach()
endfunction()

function(ADD_LIB_MODULE target_name)
    foreach(lib_name IN LISTS ARGV)
        if(NOT lib_name STREQUAL ${target_name})
            target_include_directories(${target_name} PUBLIC
                ${CMAKE_SOURCE_DIR}/lib/${lib_name}/include
            )
            target_link_libraries(${target_name} ${lib_name})
        endif()
    endforeach()
endfunction()