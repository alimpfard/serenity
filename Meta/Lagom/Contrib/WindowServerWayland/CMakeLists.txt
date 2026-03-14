if(NOT EMSCRIPTEN)
    add_compile_options(-DAK_DONT_REPLACE_STD)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

    # Generate xdg-shell protocol
    set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
        COMMAND wayland-scanner client-header ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
        DEPENDS ${XDG_SHELL_XML}
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
        COMMAND wayland-scanner private-code ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
        DEPENDS ${XDG_SHELL_XML}
    )

    # Generate IPC endpoints from WindowServer .ipc files
    set(WS_IPC_DIR "${SERENITY_PROJECT_ROOT}/Userland/Services/WindowServer")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/WindowServerEndpoint.h
        COMMAND $<TARGET_FILE:Lagom::IPCCompiler> ${WS_IPC_DIR}/WindowServer.ipc -o ${CMAKE_CURRENT_BINARY_DIR}/WindowServerEndpoint.h
        DEPENDS Lagom::IPCCompiler ${WS_IPC_DIR}/WindowServer.ipc
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/WindowClientEndpoint.h
        COMMAND $<TARGET_FILE:Lagom::IPCCompiler> ${WS_IPC_DIR}/WindowClient.ipc -o ${CMAKE_CURRENT_BINARY_DIR}/WindowClientEndpoint.h
        DEPENDS Lagom::IPCCompiler ${WS_IPC_DIR}/WindowClient.ipc
    )

    # The generated C file needs different warning flags
    set_source_files_properties(
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
        PROPERTIES COMPILE_OPTIONS "-Wno-error"
    )

    add_executable(WindowServerWayland
        main.cpp
        ScreenLayoutIPC.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
    )

    # Mark generated files so CMake knows they're expected
    set_source_files_properties(
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/WindowServerEndpoint.h
        ${CMAKE_CURRENT_BINARY_DIR}/WindowClientEndpoint.h
        PROPERTIES GENERATED TRUE
    )

    # main.cpp includes the generated headers
    target_sources(WindowServerWayland PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/WindowServerEndpoint.h
        ${CMAKE_CURRENT_BINARY_DIR}/WindowClientEndpoint.h
    )

    target_include_directories(WindowServerWayland PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${SERENITY_PROJECT_ROOT}/Userland/Services
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
    )

    target_link_libraries(WindowServerWayland PRIVATE
        LibMain LibCore LibGfx LibIPC
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
    )
endif()
