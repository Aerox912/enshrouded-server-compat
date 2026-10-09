cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" ADAPTER_VERSION LIMIT_COUNT 1)
project(xhl_server_adapter VERSION ${ADAPTER_VERSION} LANGUAGES C CXX ASM_MASM)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 is required")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(vendor/minhook-1.3.4 EXCLUDE_FROM_ALL)
add_library(adapter_core STATIC src/adapter.cpp src/extras.cpp src/pickup_delay.asm)
target_link_libraries(adapter_core PUBLIC minhook bcrypt)
target_include_directories(adapter_core PUBLIC src)
target_compile_definitions(adapter_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_library(dbghelp SHARED src/proxy.cpp src/dbghelp.def)
target_link_libraries(dbghelp PRIVATE adapter_core)
add_executable(adapter_tests tests/adapter_tests.cpp)
target_link_libraries(adapter_tests PRIVATE adapter_core)
add_executable(extras_tests tests/extras_tests.cpp tests/extras_probe.asm)
target_link_libraries(extras_tests PRIVATE adapter_core)
foreach(t adapter_core dbghelp adapter_tests extras_tests)
  target_compile_options(${t} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
endforeach()

add_executable(cloud_checks tests/cloud_checks.cpp)
target_link_libraries(cloud_checks PRIVATE adapter_core)
enable_testing()
add_test(NAME cloud_checks COMMAND cloud_checks)

# This harness is deliberately separate from dbghelp and server packages until
# authenticated identity and client activation have passed live acceptance.
option(BUILD_FLIGHT_EXPERIMENT "Build the offline server glide-hook experiment" OFF)
if(BUILD_FLIGHT_EXPERIMENT)
  set(SHROUDTOPIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../shroudtopia" CACHE PATH "Existing Shroudtopia source and Steam headers")
  add_library(flight_session STATIC src/flight_session.cpp src/flight_identity.cpp)
  target_include_directories(flight_session PUBLIC src)
  target_compile_options(flight_session PRIVATE /W4 /WX /EHsc)
  add_executable(flight_session_tests tests/flight_session_tests.cpp)
  target_link_libraries(flight_session_tests PRIVATE flight_session)
  target_compile_options(flight_session_tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME flight_session_tests COMMAND flight_session_tests)
  add_executable(flight_identity_tests tests/flight_identity_tests.cpp)
  target_link_libraries(flight_identity_tests PRIVATE flight_session)
  target_compile_options(flight_identity_tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME flight_identity_tests COMMAND flight_identity_tests)
  add_executable(flight_worker_tests tests/flight_worker_tests.cpp)
  target_include_directories(flight_worker_tests PRIVATE src)
  target_include_directories(flight_worker_tests SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/include" "${SHROUDTOPIA_ROOT}/example-mods/flight_mod")
  target_compile_definitions(flight_worker_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(flight_worker_tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME flight_worker_tests COMMAND flight_worker_tests)
  add_executable(callback_gate_tests tests/callback_gate_tests.cpp)
  target_include_directories(callback_gate_tests PRIVATE src)
  target_compile_options(callback_gate_tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME callback_gate_tests COMMAND callback_gate_tests)
  add_library(flight_native STATIC src/flight_native.cpp src/flight_hook.cpp src/flight_pitch.asm)
  target_link_libraries(flight_native PUBLIC adapter_core flight_session)
  target_include_directories(flight_native SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/third-party/steam")
  target_compile_options(flight_native PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_library(flight_server_dev SHARED src/proxy.cpp src/flight_server_dev.def)
  target_compile_definitions(flight_server_dev PRIVATE FLIGHT_DEVELOPMENT_RUNTIME)
  target_link_libraries(flight_server_dev PRIVATE flight_native)
  add_library(flight_client_dev SHARED src/flight_client.cpp)
  add_library(flight_client_common STATIC src/flight_client_host.cpp)
  target_link_libraries(flight_client_common PUBLIC adapter_core flight_session user32 gdi32)
  target_include_directories(flight_client_common SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/include" "${SHROUDTOPIA_ROOT}/example-mods/flight_mod")
  target_compile_options(flight_client_common PRIVATE /W4 /WX /EHsc /guard:cf)
  target_link_libraries(flight_client_dev PRIVATE flight_client_common)
  target_include_directories(flight_client_dev SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/third-party/steam"
    "${SHROUDTOPIA_ROOT}/include" "${SHROUDTOPIA_ROOT}/example-mods/flight_mod")
  target_compile_options(flight_client_dev PRIVATE /W4 /WX /EHsc /guard:cf)
  add_executable(flight_tests tests/flight_tests.cpp tests/flight_probe.asm src/flight_hook.cpp src/flight_pitch.asm)
  target_link_libraries(flight_tests PRIVATE adapter_core)
  target_compile_options(flight_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
endif()

option(BUILD_CREATIVE_EXPERIMENT "Build isolated Creative development adapter" OFF)
if(BUILD_CREATIVE_EXPERIMENT)
  if(NOT BUILD_FLIGHT_EXPERIMENT)
    message(FATAL_ERROR "Creative development requires the shared flight identity runtime")
  endif()
  set(CREATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../enshrouded-creative-mode" CACHE PATH "Independent Creative source")
  add_subdirectory("${CREATIVE_ROOT}" creative)
  add_library(creative_flight_server_g STATIC
    src/creative_flight_server_runtime.cpp
    src/creative_flight_g_authority.cpp
    src/creative_flight_g_integration.cpp
    src/creative_flight_call_transaction.cpp
    src/creative_flight_call_service.cpp
    src/creative_flight_dispatch_bridge.asm)
  target_include_directories(creative_flight_server_g PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(creative_flight_server_g PUBLIC flight_session minhook bcrypt)
  target_compile_definitions(creative_flight_server_g PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(creative_flight_server_g PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  set_source_files_properties(src/creative_flight_server_runtime.cpp PROPERTIES COMPILE_OPTIONS "/EHa")
  add_library(creative_flight_client_g STATIC
    src/creative_flight_call_transaction.cpp
    src/creative_flight_call_service.cpp
    "${CREATIVE_ROOT}/src/native_client_g_runtime.cpp"
    "${CREATIVE_ROOT}/src/native_client_g_owner.cpp"
    "${CREATIVE_ROOT}/src/native_client_g_dispatch_x64.asm")
  target_include_directories(creative_flight_client_g PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CREATIVE_ROOT}/include")
  target_include_directories(creative_flight_client_g SYSTEM PRIVATE
    "${SHROUDTOPIA_ROOT}/third-party/steam")
  target_link_libraries(creative_flight_client_g PUBLIC flight_session minhook bcrypt)
  target_compile_definitions(creative_flight_client_g PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(creative_flight_client_g PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  set_source_files_properties("${CREATIVE_ROOT}/src/native_client_g_runtime.cpp"
    PROPERTIES COMPILE_OPTIONS "/EHa")
  add_library(creative_native STATIC "${CREATIVE_ROOT}/src/native_runtime.cpp" "${CREATIVE_ROOT}/src/native_inventory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_effects.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/no_cost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_effects_runtime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_building_cost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_building_cost_runtime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_consumable_cost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_consumable_diagnostic.cpp"
    "${CREATIVE_ROOT}/src/native_gear_payload.cpp"
    "${CREATIVE_ROOT}/src/native_gear_serializer.cpp")
  target_link_libraries(creative_native PUBLIC creative_core flight_native native_time_runtime native_time_command creative_flight_server_g)
  target_include_directories(creative_native SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/third-party/steam")
  target_compile_options(creative_native PRIVATE /W4 /WX /EHsc /guard:cf)
  add_executable(native_cost_tests tests/native_cost_tests.cpp src/native_effects.cpp src/no_cost.cpp)
  target_link_libraries(native_cost_tests PRIVATE flight_session)
  target_compile_options(native_cost_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_cost_effects COMMAND native_cost_tests)
  add_library(native_time_policy STATIC native_time/src/time_control.cpp)
  target_include_directories(native_time_policy PUBLIC native_time/include)
  target_link_libraries(native_time_policy PUBLIC flight_session)
  target_compile_options(native_time_policy PRIVATE /W4 /WX /EHsc /guard:cf)
  add_library(native_time_command STATIC native_time/src/server_command.cpp)
  target_include_directories(native_time_command PUBLIC native_time/include)
  target_link_libraries(native_time_command PUBLIC native_time_policy)
  target_compile_options(native_time_command PRIVATE /W4 /WX /EHsc /guard:cf)
  add_library(native_time_runtime STATIC native_time/src/callback_writer.cpp
    native_time/src/read_only_observer.cpp native_time/src/read_only_server_observer.cpp
    native_time/src/runtime_adapter.cpp)
  target_include_directories(native_time_runtime PUBLIC native_time/include
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook-1.3.4/include")
  target_link_libraries(native_time_runtime PUBLIC native_time_policy adapter_core)
  target_compile_options(native_time_runtime PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_executable(native_time_tests native_time/tests/time_control_tests.cpp)
  target_link_libraries(native_time_tests PRIVATE native_time_policy)
  target_compile_options(native_time_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_policy COMMAND native_time_tests)
  add_executable(native_time_server_command_tests native_time/tests/server_command_tests.cpp)
  target_link_libraries(native_time_server_command_tests PRIVATE native_time_command)
  target_compile_options(native_time_server_command_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_server_commands COMMAND native_time_server_command_tests)
  add_library(creative_server_dev SHARED src/proxy.cpp src/flight_server_dev.def)
  target_compile_definitions(creative_server_dev PRIVATE FLIGHT_DEVELOPMENT_RUNTIME CREATIVE_DEVELOPMENT_RUNTIME)
  target_link_libraries(creative_server_dev PRIVATE creative_native)
  add_library(creative_client_dev SHARED
    "${CREATIVE_ROOT}/src/native_client.cpp"
    "${CREATIVE_ROOT}/src/native_menu.cpp"
    "${CREATIVE_ROOT}/src/native_cost_preview.cpp"
    "${CREATIVE_ROOT}/src/native_local_observers.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer.cpp"
    "${CREATIVE_ROOT}/src/native_drag_observer.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer_x64.asm"
    "${CREATIVE_ROOT}/src/native_drag_observer_x64.asm")
  target_link_libraries(creative_client_dev PRIVATE creative_core flight_client_common creative_flight_client_g minhook bcrypt)
  target_include_directories(creative_client_dev PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_include_directories(creative_client_dev SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/third-party/steam" "${SHROUDTOPIA_ROOT}/third-party/nlohmann-json/single_include" "${SHROUDTOPIA_ROOT}/include" "${SHROUDTOPIA_ROOT}/example-mods/flight_mod")
  target_compile_options(creative_client_dev PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_executable(creative_client_state_tests "${CREATIVE_ROOT}/tests/native_client_state_tests.cpp")
  target_link_libraries(creative_client_state_tests PRIVATE creative_core)
  target_include_directories(creative_client_state_tests SYSTEM PRIVATE "${SHROUDTOPIA_ROOT}/third-party/nlohmann-json/single_include")
  target_compile_definitions(creative_client_state_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(creative_client_state_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_client_state COMMAND creative_client_state_tests)
  add_executable(native_client_g_runtime_tests
    "${CREATIVE_ROOT}/tests/native_client_g_runtime_tests.cpp"
    "${CREATIVE_ROOT}/tests/native_client_g_dispatch_fixture_x64.asm")
  target_link_libraries(native_client_g_runtime_tests PRIVATE creative_flight_client_g)
  target_compile_options(native_client_g_runtime_tests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHa;/guard:cf>")
  add_test(NAME native_client_g_runtime COMMAND native_client_g_runtime_tests)
  add_executable(native_client_g_dispatch_tests
    "${CREATIVE_ROOT}/tests/native_client_g_dispatch_tests.cpp"
    "${CREATIVE_ROOT}/tests/native_client_g_dispatch_fixture_x64.asm")
  target_link_libraries(native_client_g_dispatch_tests PRIVATE creative_flight_client_g)
  target_compile_options(native_client_g_dispatch_tests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHa;/guard:cf>")
  add_test(NAME native_client_g_dispatch COMMAND native_client_g_dispatch_tests)
  add_executable(native_client_g_binding_tests
    "${CREATIVE_ROOT}/tests/native_client_g_binding_tests.cpp")
  target_link_libraries(native_client_g_binding_tests PRIVATE creative_core creative_flight_client_g)
  target_compile_options(native_client_g_binding_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_client_g_binding COMMAND native_client_g_binding_tests)
  add_executable(native_client_g_phase_status_tests
    "${CREATIVE_ROOT}/tests/native_client_g_phase_status_tests.cpp")
  target_link_libraries(native_client_g_phase_status_tests PRIVATE creative_flight_client_g)
  target_compile_options(native_client_g_phase_status_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_client_g_phase_status COMMAND native_client_g_phase_status_tests)
  add_executable(native_client_g_owner_status_tests
    "${CREATIVE_ROOT}/tests/native_client_g_owner_status_tests.cpp")
  target_link_libraries(native_client_g_owner_status_tests PRIVATE creative_flight_client_g)
  target_compile_options(native_client_g_owner_status_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_native_client_g_owner_status COMMAND native_client_g_owner_status_tests)
  add_executable(creative_gear_options_cache_tests
    "${CREATIVE_ROOT}/tests/native_gear_options_cache_tests.cpp")
  target_link_libraries(creative_gear_options_cache_tests PRIVATE creative_core)
  target_compile_definitions(creative_gear_options_cache_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(creative_gear_options_cache_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_native_gear_options_cache COMMAND creative_gear_options_cache_tests)
  add_executable(native_hud_observer_tests
    "${CREATIVE_ROOT}/tests/native_hud_observer_tests.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer.cpp")
  target_include_directories(native_hud_observer_tests PRIVATE "${CREATIVE_ROOT}/include")
  target_compile_options(native_hud_observer_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME native_hud_observer COMMAND native_hud_observer_tests)

  add_executable(native_hud_observer_abi_tests
    "${CREATIVE_ROOT}/tests/native_hud_observer_abi_tests.cpp"
    "${CREATIVE_ROOT}/tests/native_hud_observer_abi_tests_x64.asm"
    "${CREATIVE_ROOT}/src/native_hud_observer.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer_x64.asm")
  target_include_directories(native_hud_observer_abi_tests PRIVATE "${CREATIVE_ROOT}/include")
  target_compile_options(native_hud_observer_abi_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME native_hud_observer_abi COMMAND native_hud_observer_abi_tests)

  add_executable(native_drag_observer_tests
    "${CREATIVE_ROOT}/tests/native_drag_observer_tests.cpp"
    "${CREATIVE_ROOT}/tests/native_drag_observer_abi_tests_x64.asm"
    "${CREATIVE_ROOT}/src/native_drag_observer.cpp"
    "${CREATIVE_ROOT}/src/native_drag_observer_x64.asm")
  target_include_directories(native_drag_observer_tests PRIVATE "${CREATIVE_ROOT}/include")
  target_link_libraries(native_drag_observer_tests PRIVATE bcrypt)
  target_compile_definitions(native_drag_observer_tests PRIVATE CREATIVE_NATIVE_DRAG_OBSERVER_TESTING)
  target_compile_options(native_drag_observer_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME native_drag_observer COMMAND native_drag_observer_tests)

  add_executable(native_local_observers_tests
    "${CREATIVE_ROOT}/tests/native_local_observers_tests.cpp"
    "${CREATIVE_ROOT}/src/native_local_observers.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer.cpp"
    "${CREATIVE_ROOT}/src/native_drag_observer.cpp"
    "${CREATIVE_ROOT}/src/native_hud_observer_x64.asm"
    "${CREATIVE_ROOT}/src/native_drag_observer_x64.asm")
  target_include_directories(native_local_observers_tests PRIVATE "${CREATIVE_ROOT}/include")
  target_link_libraries(native_local_observers_tests PRIVATE minhook bcrypt)
  target_compile_definitions(native_local_observers_tests PRIVATE
    CREATIVE_NATIVE_LOCAL_OBSERVERS_TESTING CREATIVE_NATIVE_DRAG_OBSERVER_TESTING)
  target_compile_options(native_local_observers_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME native_local_observers_default_off COMMAND native_local_observers_tests)
  add_test(NAME native_local_observers_foreign_drag COMMAND native_local_observers_tests foreign-drag)
  add_test(NAME native_local_observers_drag_create_failure COMMAND native_local_observers_tests drag-create-failure)
  add_test(NAME native_local_observers_activation_rollback COMMAND native_local_observers_tests activation-rollback)
  add_test(NAME native_local_observers_disable_retry COMMAND native_local_observers_tests disable-retry)
  add_test(NAME native_local_observers_remove_retry COMMAND native_local_observers_tests remove-retry)

  add_executable(creative_flight_g_authority_tests tests/creative_flight_g_authority_tests.cpp)
  target_link_libraries(creative_flight_g_authority_tests PRIVATE creative_flight_server_g)
  target_compile_options(creative_flight_g_authority_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_g_authority COMMAND creative_flight_g_authority_tests)

  add_executable(creative_flight_g_integration_tests tests/creative_flight_g_integration_tests.cpp)
  target_link_libraries(creative_flight_g_integration_tests PRIVATE creative_flight_server_g)
  target_compile_options(creative_flight_g_integration_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_g_integration COMMAND creative_flight_g_integration_tests)

  add_executable(creative_flight_server_runtime_tests tests/creative_flight_server_runtime_tests.cpp
    tests/creative_flight_dispatch_fixture.asm)
  target_link_libraries(creative_flight_server_runtime_tests PRIVATE creative_flight_server_g)
  target_compile_options(creative_flight_server_runtime_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME creative_flight_server_runtime COMMAND creative_flight_server_runtime_tests)

  add_executable(creative_flight_dispatch_bridge_tests tests/creative_flight_dispatch_bridge_tests.cpp
    tests/creative_flight_dispatch_fixture.asm)
  target_link_libraries(creative_flight_dispatch_bridge_tests PRIVATE creative_flight_server_g)
  target_compile_options(creative_flight_dispatch_bridge_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/guard:cf>")
  add_test(NAME creative_flight_dispatch_bridge COMMAND creative_flight_dispatch_bridge_tests)

  add_executable(creative_client_life_tests tests/creative_client_life_tests.cpp)
  target_include_directories(creative_client_life_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(creative_client_life_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_client_life_reader COMMAND creative_client_life_tests)

  add_executable(native_client_life_tests "${CREATIVE_ROOT}/tests/native_client_life_tests.cpp")
  target_link_libraries(native_client_life_tests PRIVATE creative_core flight_session)
  target_compile_options(native_client_life_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_native_client_life COMMAND native_client_life_tests)

  add_executable(creative_flight_local_tests tests/creative_flight_local_tests.cpp)
  target_include_directories(creative_flight_local_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(creative_flight_local_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_local_reader COMMAND creative_flight_local_tests)

  add_executable(creative_flight_movement_tests tests/creative_flight_movement_tests.cpp)
  target_include_directories(creative_flight_movement_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(creative_flight_movement_tests PRIVATE flight_session)
  target_compile_options(creative_flight_movement_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_movement_policy COMMAND creative_flight_movement_tests)

  add_executable(creative_flight_native_movement_tests tests/creative_flight_native_movement_tests.cpp)
  target_include_directories(creative_flight_native_movement_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(creative_flight_native_movement_tests PRIVATE flight_session)
  target_compile_options(creative_flight_native_movement_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_native_movement_policy COMMAND creative_flight_native_movement_tests)

  add_executable(creative_flight_probe_tests tests/creative_flight_probe_tests.cpp)
  target_include_directories(creative_flight_probe_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(creative_flight_probe_tests PRIVATE flight_session)
  target_compile_definitions(creative_flight_probe_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(creative_flight_probe_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME creative_flight_probe_contract COMMAND creative_flight_probe_tests)

  add_executable(native_building_cost_policy_tests tests/native_building_cost_tests.cpp src/native_building_cost.cpp)
  target_include_directories(native_building_cost_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(native_building_cost_policy_tests PRIVATE flight_session)
  target_compile_options(native_building_cost_policy_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_building_cost_policy COMMAND native_building_cost_policy_tests)

  add_executable(native_consumable_cost_policy_tests tests/native_consumable_cost_tests.cpp src/native_consumable_cost.cpp)
  target_include_directories(native_consumable_cost_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(native_consumable_cost_policy_tests PRIVATE flight_session)
  target_compile_options(native_consumable_cost_policy_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_consumable_cost_policy COMMAND native_consumable_cost_policy_tests)

  add_executable(native_cost_hook_lifecycle_tests tests/native_cost_hook_lifecycle_tests.cpp)
  target_compile_options(native_cost_hook_lifecycle_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_cost_hook_lifecycle COMMAND native_cost_hook_lifecycle_tests)

  add_executable(adapter_hook_lifecycle_tests tests/adapter_hook_lifecycle_tests.cpp)
  target_compile_options(adapter_hook_lifecycle_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME adapter_hook_lifecycle COMMAND adapter_hook_lifecycle_tests)

  add_executable(native_gear_payload_tests
    "${CREATIVE_ROOT}/tests/native_gear_payload_tests.cpp"
    "${CREATIVE_ROOT}/src/native_gear_payload.cpp")
  target_link_libraries(native_gear_payload_tests PRIVATE creative_core)
  target_compile_options(native_gear_payload_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_gear_payload COMMAND native_gear_payload_tests)

  add_executable(native_gear_serializer_tests
    "${CREATIVE_ROOT}/tests/native_gear_serializer_tests.cpp"
    "${CREATIVE_ROOT}/src/native_gear_payload.cpp"
    "${CREATIVE_ROOT}/src/native_gear_serializer.cpp")
  target_link_libraries(native_gear_serializer_tests PRIVATE creative_core)
  target_compile_options(native_gear_serializer_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_gear_serializer COMMAND native_gear_serializer_tests)

  add_executable(native_runtime_adapter_readiness_tests
    "${CREATIVE_ROOT}/tests/native_runtime_adapter_readiness_tests.cpp")
  target_include_directories(native_runtime_adapter_readiness_tests PRIVATE
    "${CREATIVE_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(native_runtime_adapter_readiness_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_runtime_adapter_readiness COMMAND native_runtime_adapter_readiness_tests)

  add_executable(native_consumable_runtime_tests tests/native_consumable_runtime_tests.cpp src/native_consumable_cost.cpp)
  target_link_libraries(native_consumable_runtime_tests PRIVATE flight_session)
  target_compile_options(native_consumable_runtime_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_consumable_runtime COMMAND native_consumable_runtime_tests)

  add_executable(native_consumable_diagnostic_tests tests/native_consumable_diagnostic_tests.cpp)
  target_compile_definitions(native_consumable_diagnostic_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(native_consumable_diagnostic_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_consumable_diagnostic COMMAND native_consumable_diagnostic_tests)

  add_executable(native_time_callback_writer_tests native_time/tests/callback_writer_tests.cpp native_time/src/callback_writer.cpp)
  target_link_libraries(native_time_callback_writer_tests PRIVATE native_time_policy)
  target_compile_options(native_time_callback_writer_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_callback_writer COMMAND native_time_callback_writer_tests)

  add_executable(native_time_observer_tests native_time/tests/read_only_observer_tests.cpp native_time/src/read_only_observer.cpp)
  target_include_directories(native_time_observer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native_time/include")
  target_link_libraries(native_time_observer_tests PRIVATE flight_session)
  target_compile_options(native_time_observer_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_observer COMMAND native_time_observer_tests)

  # This adapter test supplies MinHook API stubs; do not link the real minhook target.
  add_executable(native_time_server_observer_tests native_time/tests/read_only_server_observer_tests.cpp native_time/src/read_only_server_observer.cpp native_time/src/read_only_observer.cpp native_time/src/runtime_adapter.cpp native_time/src/callback_writer.cpp)
  target_include_directories(native_time_server_observer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/native_time/include" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook-1.3.4/include")
  target_link_libraries(native_time_server_observer_tests PRIVATE flight_session)
  target_link_libraries(native_time_server_observer_tests PRIVATE native_time_policy)
  target_compile_definitions(native_time_server_observer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(native_time_server_observer_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_server_observer COMMAND native_time_server_observer_tests)

  add_executable(native_time_server_runtime_tests native_time/tests/server_runtime_integration_tests.cpp
    native_time/src/read_only_server_observer.cpp native_time/src/read_only_observer.cpp
    native_time/src/runtime_adapter.cpp native_time/src/callback_writer.cpp)
  target_include_directories(native_time_server_runtime_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_time/include" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook-1.3.4/include")
  target_link_libraries(native_time_server_runtime_tests PRIVATE flight_session native_time_policy)
  target_compile_definitions(native_time_server_runtime_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(native_time_server_runtime_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_server_runtime_integration COMMAND native_time_server_runtime_tests)

  add_executable(native_time_runtime_adapter_tests
    native_time/tests/runtime_adapter_tests.cpp native_time/src/runtime_adapter.cpp
    native_time/src/read_only_observer.cpp native_time/src/callback_writer.cpp)
  target_include_directories(native_time_runtime_adapter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/native_time/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook-1.3.4/include")
  target_link_libraries(native_time_runtime_adapter_tests PRIVATE flight_session native_time_policy)
  target_compile_definitions(native_time_runtime_adapter_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(native_time_runtime_adapter_tests PRIVATE /W4 /WX /EHsc /guard:cf)
  add_test(NAME native_time_runtime_adapter COMMAND native_time_runtime_adapter_tests)
  add_test(NAME native_time_runtime_adapter_create_failure
    COMMAND native_time_runtime_adapter_tests --create-failure)
endif()
