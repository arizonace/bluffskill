qt_add_executable(bluffskill-client
    human_player.cpp
    main.cpp
)

target_link_libraries(bluffskill-client PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)
