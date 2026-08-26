add_library(bluffskill_cards STATIC
    src/card.cpp
    src/deck.cpp
)

target_include_directories(bluffskill_cards
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(bluffskill_cards PUBLIC cxx_std_20)
