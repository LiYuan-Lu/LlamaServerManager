cmake_minimum_required(VERSION 3.24)
project(LlamaServerManager VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Quick QuickControls2 Test LinguistTools)
qt_standard_project_setup(REQUIRES 6.5)
if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_library(lsm_core STATIC core/Profile.cpp core/ProfileStore.cpp core/LogStore.cpp core/LlamaProcessManager.cpp
  core/RestartPolicy.cpp core/HealthMonitor.cpp core/ServerSupervisor.cpp
  core/SystemMonitor.cpp core/GpuMonitor.cpp core/MetricsCollector.cpp core/AppSettings.cpp)
target_include_directories(lsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lsm_core PUBLIC Qt6::Core Qt6::Network)
add_library(lsm_ipc STATIC ipc/Protocol.cpp ipc/AgentServer.cpp ipc/AgentClient.cpp ipc/LogModel.cpp)
target_link_libraries(lsm_ipc PUBLIC lsm_core)
qt_add_executable(llama-manager-agent apps/agent/main.cpp)
target_link_libraries(llama-manager-agent PRIVATE lsm_ipc)
set(LSM_GUI_QML apps/gui/qml/Main.qml apps/gui/qml/ModelCard.qml apps/gui/qml/AddCard.qml apps/gui/qml/ModelPage.qml
  apps/gui/qml/ServerControls.qml apps/gui/qml/StateBadge.qml apps/gui/qml/ProfileEditor.qml apps/gui/qml/LogView.qml
  apps/gui/qml/RestartHistoryView.qml apps/gui/qml/ResourcePanel.qml apps/gui/qml/MetricTile.qml apps/gui/qml/LineChart.qml
  apps/gui/qml/UsageLabel.qml apps/gui/qml/Theme.js)
# QML files are listed as sources so lupdate scans them for qsTr().
qt_add_executable(llama-manager WIN32 apps/gui/main.cpp apps/gui/LanguageManager.cpp ${LSM_GUI_QML})
target_link_libraries(llama-manager PRIVATE lsm_ipc Qt6::Quick Qt6::QuickControls2)
qt_add_resources(llama-manager qml PREFIX / FILES ${LSM_GUI_QML})
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
  foreach(case restartPolicy healthMonitor systemMonitor gpuMonitor metricsCollector agentMetricsAndSettings logModel watchdogStartupGrace watchdogCrashRecovery watchdogHealthRestart watchdogLimitsAndManualStop protocolFraming persistenceAndValidation boundedLogs processLifecycle failuresAndCancellation agentAndGuiReconnect guiDetachedAgent)
    add_test(NAME ${case} COMMAND lsm-tests ${case} -o ${case}-results.txt,txt -o -,txt)
    set_tests_properties(${case} PROPERTIES TIMEOUT 180)
  endforeach()
endif()
install(TARGETS llama-manager llama-manager-agent RUNTIME DESTINATION bin)
