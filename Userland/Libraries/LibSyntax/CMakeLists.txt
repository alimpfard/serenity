set(SOURCES
    Highlighter.cpp
    Language.cpp
    TextMateHighlighter.cpp
)

serenity_lib(LibSyntax syntax)
target_link_libraries(LibSyntax PRIVATE LibGfx LibRegex LibXML)
