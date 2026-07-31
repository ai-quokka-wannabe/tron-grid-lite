add_library(bvh STATIC
    src/bvh.cpp
)

target_include_directories(bvh PUBLIC include)
target_compile_features(bvh PUBLIC cxx_std_20)
target_link_libraries(bvh PUBLIC math)

add_subdirectory(tests)
