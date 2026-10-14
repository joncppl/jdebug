add_executable(example_c example.c common.c)
target_link_libraries(example_c jdebug)

add_executable(example_cpp example.cpp common.c)
target_link_libraries(example_cpp jdebug)
target_compile_features(example_cpp PRIVATE cxx_std_17)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(example_c PRIVATE -Wno-div-by-zero)
    target_compile_options(example_cpp PRIVATE -Wno-div-by-zero)
endif ()


if (WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(example_c PRIVATE "-municode")

    target_link_options(example_cpp PRIVATE "-municode")
endif ()
