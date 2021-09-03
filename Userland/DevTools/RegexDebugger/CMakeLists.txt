serenity_component(
    RegexDebugger
    RECOMMENDED
    TARGETS RegexDebugger
)

compile_gml(main_window.gml MainWindowGML.h main_window_gml)

set(SOURCES
    main.cpp
)

set(GENERATED_SOURCES
        MainWindowGML.h
)

# FIXME: Set a real icon pls
serenity_app(RegexDebugger ICON app-playground)
target_link_libraries(RegexDebugger LibGUI LibRegex)
