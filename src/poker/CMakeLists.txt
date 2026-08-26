add_library(bluffskill_poker STATIC
    src/api_player.cpp
    src/house.cpp
    src/reference_player.cpp
    src/table.cpp
)

target_include_directories(bluffskill_poker
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bluffskill_poker PUBLIC bluffskill_cards)
target_compile_features(bluffskill_poker PUBLIC cxx_std_20)
