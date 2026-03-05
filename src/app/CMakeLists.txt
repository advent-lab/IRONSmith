add_executable(ironsmith
        main.cpp
)

target_link_libraries(ironsmith
        PRIVATE
        ${QT6_REQUIRED_COMPONENTS}
        ExtensionSystem
        Utils
        CorePlugin
)
#add_dependencies(ironsmith
#        CanvasPlugin
#)
set_target_properties(ironsmith PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${IRONSMITH_BUILD_BINDIR}"
        CXX_EXTENSIONS OFF
)

if (WIN32)
    # Copy DLL dependencies to bin/ so the Windows loader can find them at startup.
    # Plugins loaded at runtime are handled by addLibrarySearchPaths() in main.cpp.
    add_custom_command(TARGET ironsmith POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ExtensionSystem>
            $<TARGET_FILE:Utils>
            $<TARGET_FILE:CorePlugin>
            $<TARGET_FILE_DIR:ironsmith>
    )
endif()

if (BUILD_WITH_TESTS)
    target_sources(ironsmith PRIVATE
        ${PROJECT_SOURCE_DIR}/tests/hlir_bridge/BridgeTests.cpp
    )
    target_include_directories(ironsmith PRIVATE
        ${PROJECT_SOURCE_DIR}/tests/hlir_bridge
    )
    target_link_libraries(ironsmith PRIVATE
        HlirCppBridge
        CodeGenBridge
    )
    target_compile_definitions(ironsmith PRIVATE IRONSMITH_WITH_BRIDGE_TESTS)

    if (WIN32)
        # Copy bridge DLL dependencies to bin/ so --test-bridges works at runtime.
        add_custom_command(TARGET ironsmith POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:HlirCppBridge>
                $<TARGET_FILE:CodeGenBridge>
                $<TARGET_FILE_DIR:ironsmith>
        )
    endif()
endif()

if (TARGET iron_sanitize_flags)
    target_link_libraries(ironsmith PRIVATE iron_sanitize_flags)
endif()

install(TARGETS ironsmith
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)