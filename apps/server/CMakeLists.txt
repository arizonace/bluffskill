qt_add_executable(bluffskill-server
    main.cpp
)

target_link_libraries(bluffskill-server PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::HttpServer
    bluffskill_app_config
    bluffskill_poker
)
