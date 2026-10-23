cmake_minimum_required(VERSION 3.24)
project(LlamaServerManager VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Widgets Quick QuickControls2 Test LinguistTools)
qt_standard_project_setup(REQUIRES 6.5)
if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_library(lsm_core STATIC core/Profile.cpp core/ProfileStore.cpp core/LogStore.cpp core/LlamaProcessManager.cpp
  core/RestartPolicy.cpp core/HealthMonitor.cpp core/ServerSupervisor.cpp
  core/SystemMonitor.cpp core/GpuMonitor.cpp core/MetricsCollector.cpp core/AppSettings.cpp core/LoginStartup.cpp
  core/OpencodeConfig.cpp)
target_include_directories(lsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lsm_core PUBLIC Qt6::Core Qt6::Network)
add_library(lsm_ipc STATIC ipc/Protocol.cpp ipc/AgentServer.cpp ipc/AgentClient.cpp ipc/LogModel.cpp)
target_link_libraries(lsm_ipc PUBLIC lsm_core)
qt_add_executable(llama-manager-agent apps/agent/main.cpp)
target_link_libraries(llama-manager-agent PRIVATE lsm_ipc)

# Windows: icon and version information for Explorer.
function(lsm_windows_resources target name description)
  if(WIN32)
    set(LSM_ICON "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/icons/app.ico")
    set(LSM_RC_NAME "${name}")
    set(LSM_RC_DESCRIPTION "${description}")
    configure_file(packaging/windows/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc")
  endif()
endfunction()
lsm_windows_resources(llama-manager-agent llama-manager-agent "Llama Server Manager Agent")
set(LSM_GUI_QML apps/gui/qml/Main.qml apps/gui/qml/ModelCard.qml apps/gui/qml/AddCard.qml apps/gui/qml/ModelPage.qml
  apps/gui/qml/ServerControls.qml apps/gui/qml/StateBadge.qml apps/gui/qml/ProfileEditor.qml apps/gui/qml/LogView.qml
  apps/gui/qml/RestartHistoryView.qml apps/gui/qml/ResourcePanel.qml apps/gui/qml/MetricTile.qml apps/gui/qml/LineChart.qml
  apps/gui/qml/UsageLabel.qml apps/gui/qml/Theme.js)
# QML files are listed as sources so lupdate scans them for qsTr().
qt_add_executable(llama-manager WIN32 MACOSX_BUNDLE apps/gui/main.cpp apps/gui/LanguageManager.cpp
  apps/gui/SingleInstance.cpp apps/gui/TrayIcon.cpp ${LSM_GUI_QML})
lsm_windows_resources(llama-manager llama-manager "Llama Server Manager")
# macOS: an app bundle with its icon; the Agent lives next to the GUI inside the bundle, where
# the GUI looks for it, and finds the bundled Qt frameworks through its rpath.
set(LSM_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/icons/app.icns")
set_source_files_properties(${LSM_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(llama-manager PRIVATE ${LSM_ICNS})
set_target_properties(llama-manager PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Llama Server Manager"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.liyuan-lu.llama-server-manager"
  MACOSX_BUNDLE_ICON_FILE app.icns
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE)
  set_target_properties(llama-manager-agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/llama-manager.app/Contents/MacOS"
    INSTALL_RPATH "@executable_path/../Frameworks")
endif()
# Widgets provides QSystemTrayIcon and its menu on every platform.
target_link_libraries(llama-manager PRIVATE lsm_ipc Qt6::Widgets Qt6::Quick Qt6::QuickControls2)
qt_add_resources(llama-manager qml PREFIX / FILES ${LSM_GUI_QML})
qt_add_resources(llama-manager icons PREFIX /icons BASE apps/gui/icons FILES apps/gui/icons/app.png apps/gui/icons/tray-mono.png)
# Builds the .qm files and embeds them under :/translations. Run the llama-manager_lupdate
# target to refresh the .ts files after changing qsTr() strings.
qt_add_translations(llama-manager TS_FILES translations/llama-manager_zh_TW.ts RESOURCE_PREFIX /translations)
include(CTest)
if(BUILD_TESTING)
  qt_add_executable(fake-llama-server tests/FakeServer.cpp)
  target_link_libraries(fake-llama-server PRIVATE Qt6::Core Qt6::Network)
  # Stand-in NVML library so GPU monitoring can be tested without an NVIDIA GPU.
  add_library(fake-nvml SHARED tests/FakeNvml.cpp)
  qt_add_executable(lsm-tests tests/Tests.cpp)
  target_link_libraries(lsm-tests PRIVATE lsm_ipc Qt6::Test)
  target_compile_definitions(lsm-tests PRIVATE LSM_FAKE_NVML="$<TARGET_FILE:fake-nvml>")
  add_dependencies(lsm-tests fake-llama-server fake-nvml llama-manager-agent llama-manager)
  foreach(case restartPolicy healthMonitor systemMonitor gpuMonitor metricsCollector agentMetricsAndSettings logModel autoStartAndShutdown loginStartup guiSingleInstanceAndQuit watchdogStartupGrace watchdogCrashRecovery watchdogHealthRestart watchdogLimitsAndManualStop protocolFraming persistenceAndValidation boundedLogs processLifecycle failuresAndCancellation agentAndGuiReconnect guiDetachedAgent
    aliasOption opencodeConfigBuild opencodeConfigFetch)
    add_test(NAME ${case} COMMAND lsm-tests ${case} -o ${case}-results.txt,txt -o -,txt)
    set_tests_properties(${case} PROPERTIES TIMEOUT 180)
    if(WIN32)
      # Outside Qt Creator the Qt DLLs are not on PATH; put them there for ctest.
      set_tests_properties(${case} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
  endforeach()
endif()
# `cmake --install` produces a self-contained, runnable folder on every platform: the
# executables plus the Qt libraries, plugins, and QML modules they need (windeployqt on
# Windows, macdeployqt on macOS). Platform installers in packaging/ start from this folder.
include(GNUInstallDirs)
install(TARGETS llama-manager BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(APPLE)
  install(TARGETS llama-manager-agent RUNTIME DESTINATION llama-manager.app/Contents/MacOS)
else()
  install(TARGETS llama-manager-agent RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(WIN32)
  set(LSM_DOC_DIR .)
elseif(APPLE)
  set(LSM_DOC_DIR llama-manager.app/Contents/Resources)
else()
  set(LSM_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
endif()
install(FILES LICENSE packaging/THIRD_PARTY_NOTICES.txt DESTINATION ${LSM_DOC_DIR})
if(COMMAND qt_generate_deploy_qml_app_script AND Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  # The GUI deploys the Qt libraries the Agent needs too (Core, Network).
  qt_generate_deploy_qml_app_script(TARGET llama-manager OUTPUT_SCRIPT lsm_deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${lsm_deploy_script})
endif()
