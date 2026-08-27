add_library(bluffskill_app_config STATIC
    src/app_config.cpp
)

target_include_directories(bluffskill_app_config
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bluffskill_app_config PUBLIC Qt6::Core)
target_compile_features(bluffskill_app_config PUBLIC cxx_std_20)
