qt_add_executable(bluffskill-client
    main.cpp
)

target_link_libraries(bluffskill-client PRIVATE
    Qt6::Core
    Qt6::Widgets
)
