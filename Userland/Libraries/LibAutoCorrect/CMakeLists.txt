set(SOURCES
    WordTree.cpp
)

serenity_lib(LibAutoCorrect autocorrect)
target_link_libraries(LibAutoCorrect LibCore)
