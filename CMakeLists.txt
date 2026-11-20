# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)
project(Plasmid VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick Qml DBus Concurrent Sql)
function(plasmid_add_translations target resource_name)
    target_sources(${target} PRIVATE src/translations.cpp src/translations.h)
    qt_add_resources(${target} ${resource_name} PREFIX "/" FILES
        i18n/en.json i18n/pl.json i18n/de.json i18n/fr.json i18n/es.json)
endfunction()
set(PLASMID_BACKEND src/launcher.cpp src/launcher.h src/downloadsmodel.cpp src/downloadsmodel.h
    src/applicationsmodel.cpp src/applicationsmodel.h src/applicationlocation.cpp src/applicationlocation.h
    src/groups.cpp src/groups.h src/lutrislibrary.cpp src/lutrislibrary.h)

add_executable(plasmid src/main.cpp src/groupnotifications.cpp src/groupnotifications.h ${PLASMID_BACKEND})
plasmid_add_translations(plasmid translations)
set_target_properties(plasmid PROPERTIES OUTPUT_NAME plasmid-launcher)
target_link_libraries(plasmid PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Sql)
qt_add_resources(plasmid ui PREFIX "/" FILES
    qml/Main.qml qml/LauncherPanel.qml qml/ActionToast.qml qml/ResizeSkeleton.qml qml/GroupSection.qml qml/PanelButton.qml qml/AppTile.qml qml/DownloadsView.qml qml/ApplicationsView.qml qml/RevealMenu.qml qml/SearchResults.qml tests/SearchSmoke.qml)
target_compile_options(plasmid PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS plasmid RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME})
install(FILES packaging/io.github.Vem0n.Plasmid.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(plasmid-legacysettings-test tests/legacysettings_test.cpp)
    add_dependencies(plasmid-legacysettings-test plasmid)
    target_include_directories(plasmid-legacysettings-test PRIVATE src)
    target_link_libraries(plasmid-legacysettings-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME legacy-settings-migration COMMAND plasmid-legacysettings-test)
    add_executable(plasmid-translations-test tests/translations_test.cpp)
    plasmid_add_translations(plasmid-translations-test translation_catalogs)
    target_include_directories(plasmid-translations-test PRIVATE src)
    target_link_libraries(plasmid-translations-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME translations-and-system-language COMMAND plasmid-translations-test)
    set_tests_properties(translations-and-system-language PROPERTIES TIMEOUT 20)
    add_test(NAME search-providers COMMAND plasmid --smoke-test)
    set_tests_properties(search-providers PROPERTIES TIMEOUT 35
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    add_executable(plasmid-ui-test tests/ui_test.cpp ${PLASMID_BACKEND})
    plasmid_add_translations(plasmid-ui-test test_translations)
    target_include_directories(plasmid-ui-test PRIVATE src)
    target_link_libraries(plasmid-ui-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Test Qt6::Sql)
    qt_add_resources(plasmid-ui-test test_ui PREFIX "/" FILES
        qml/Main.qml qml/LauncherPanel.qml qml/ActionToast.qml qml/ResizeSkeleton.qml qml/GroupSection.qml qml/PanelButton.qml qml/AppTile.qml qml/DownloadsView.qml qml/ApplicationsView.qml qml/RevealMenu.qml qml/SearchResults.qml)
    add_test(NAME dropdown-interaction COMMAND plasmid-ui-test)
    set_tests_properties(dropdown-interaction PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    add_executable(plasmid-downloads-test tests/downloads_test.cpp ${PLASMID_BACKEND})
    target_include_directories(plasmid-downloads-test PRIVATE src)
    target_link_libraries(plasmid-downloads-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Test Qt6::Sql)
    add_test(NAME downloads-watcher-and-actions COMMAND plasmid-downloads-test)
    set_tests_properties(downloads-watcher-and-actions PROPERTIES TIMEOUT 40
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    add_executable(plasmid-applications-test tests/applications_test.cpp ${PLASMID_BACKEND})
    target_include_directories(plasmid-applications-test PRIVATE src)
    target_link_libraries(plasmid-applications-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Test Qt6::Sql)
    add_test(NAME applications-catalog-and-actions COMMAND plasmid-applications-test)
    set_tests_properties(applications-catalog-and-actions PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    add_executable(plasmid-lutris-test tests/lutris_test.cpp ${PLASMID_BACKEND})
    target_include_directories(plasmid-lutris-test PRIVATE src)
    target_link_libraries(plasmid-lutris-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Test Qt6::Sql)
    add_test(NAME lutris-library-and-actions COMMAND plasmid-lutris-test)
    set_tests_properties(lutris-library-and-actions PROPERTIES TIMEOUT 25
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    add_executable(plasmid-search-test tests/search_test.cpp ${PLASMID_BACKEND})
    target_include_directories(plasmid-search-test PRIVATE src)
    target_link_libraries(plasmid-search-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus Qt6::Concurrent Qt6::Test Qt6::Sql)
    add_test(NAME search-actions-and-reveal COMMAND plasmid-search-test)
    set_tests_properties(search-actions-and-reveal PROPERTIES TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(plasmid-groups-test tests/groups_test.cpp src/groups.cpp src/groups.h
        src/applicationsmodel.cpp src/applicationsmodel.h src/groupnotifications.cpp src/groupnotifications.h src/lutrislibrary.cpp src/lutrislibrary.h)
    target_include_directories(plasmid-groups-test PRIVATE src)
    target_link_libraries(plasmid-groups-test PRIVATE Qt6::Core Qt6::Concurrent Qt6::DBus Qt6::Test Qt6::Sql)
    add_test(NAME groups-and-local-suggestions COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:plasmid-groups-test>)
    set_tests_properties(groups-and-local-suggestions PROPERTIES TIMEOUT 30)
    add_executable(plasmid-instance-test tests/instance_test.cpp)
    target_link_libraries(plasmid-instance-test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    add_dependencies(plasmid-instance-test plasmid)
    add_test(NAME instance-shortcut-modes COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:plasmid-instance-test>)
    set_tests_properties(instance-shortcut-modes PROPERTIES TIMEOUT 20
        ENVIRONMENT "PLASMID_PRIVATE_BUS_TEST=1;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
endif()
