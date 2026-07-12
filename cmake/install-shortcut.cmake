# Runs at `cmake --install` time. Creates a per-user Start Menu
# shortcut so Windows search finds the standalone by name. Lives in
# the per-user Start Menu folder (no admin required for the shortcut
# itself) and points at the installed .exe.

set(SHORTCUT_PATH "$ENV{APPDATA}\\Microsoft\\Windows\\Start Menu\\Programs\\BluePrinter.lnk")
set(TARGET_PATH "${CMAKE_INSTALL_PREFIX}/BluePrinter.exe")
set(WORKING_DIR "${CMAKE_INSTALL_PREFIX}")

execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_SOURCE_DIR}/cmake/create-shortcut.ps1"
        -ShortcutPath "${SHORTCUT_PATH}"
        -TargetPath "${TARGET_PATH}"
        -WorkingDirectory "${WORKING_DIR}"
        -Description "BluePrinter"
    RESULT_VARIABLE POWERSHELL_RESULT
    OUTPUT_VARIABLE POWERSHELL_OUTPUT
    ERROR_VARIABLE POWERSHELL_ERROR
)

if(NOT POWERSHELL_RESULT EQUAL 0)
    message(WARNING
        "Failed to create Start Menu shortcut (exit ${POWERSHELL_RESULT}).\n"
        "  stdout: ${POWERSHELL_OUTPUT}\n"
        "  stderr: ${POWERSHELL_ERROR}\n"
        "You can create one manually: right-click BluePrinter.exe, Create shortcut, "
        "move it to %APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\, rename to BluePrinter.")
else()
    message(STATUS "Created Start Menu shortcut: ${SHORTCUT_PATH}")
endif()
