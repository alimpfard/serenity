serenity_component(
    Fire
    TARGETS Fire
)

set(SOURCES
    Fire.cpp
)

serenity_app(Fire ICON app-fire)
target_link_libraries(Fire PRIVATE LibGUI LibCore LibGfx LibMain)

if (ENABLE_JAKT)
    add_jakt_executable(JaktFire
        MAIN_SOURCE fire.jakt
        MODULE_SOURCES
            ${SERENITY_JAKT_SOURCES}
        INCLUDES ${CMAKE_SOURCE_DIR}/Userland/Libraries/Jakt
    )
    target_link_libraries(JaktFire PRIVATE LibC LibCore LibGUI LibGfx)
    target_compile_options(JaktFire PRIVATE "-DJAKT_USING_AK_AS_STANDARD_LIBRARY=1")
    set_target_properties(JaktFire PROPERTIES EXCLUDE_FROM_ALL TRUE)
    install(TARGETS JaktFire RUNTIME DESTINATION bin OPTIONAL)
    serenity_component(
        JaktFire
        RECOMMENDED
        TARGETS JaktFire
    )
endif()
