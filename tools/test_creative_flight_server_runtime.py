"""Build/run the isolated native G flight server runtime tests."""
import argparse
import subprocess
from pathlib import Path


def run(build: Path, cmake: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    ctest = cmake.with_name("ctest.exe")
    if not cmake.is_file() or not ctest.is_file():
        raise ValueError("provide the existing native Windows CMake/CTest toolchain")
    marker = build / ".gflight-server-runtime-test-root"
    if build.exists() and any(build.iterdir()) and (not marker.is_file() or marker.read_text() != str(root)):
        raise ValueError("nonempty build directory does not belong to this isolated harness")
    build.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(root))
    (build/"test-output.log").write_text("",encoding="utf-8")
    source, binary = build / "source", build / "binary"
    source.mkdir(exist_ok=True)
    (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.24)
project(gflight_server_runtime_test LANGUAGES CXX ASM_MASM)
add_executable(creative_flight_server_runtime_tests "{root.as_posix()}/tests/creative_flight_server_runtime_tests.cpp" "{root.as_posix()}/src/flight_session.cpp" "{root.as_posix()}/src/creative_flight_server_runtime.cpp" "{root.as_posix()}/src/creative_flight_dispatch_bridge.asm" "{root.as_posix()}/tests/creative_flight_dispatch_fixture.asm")
target_include_directories(creative_flight_server_runtime_tests PRIVATE "{root.as_posix()}/src")
target_compile_features(creative_flight_server_runtime_tests PRIVATE cxx_std_20)
target_compile_options(creative_flight_server_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX>)
set_source_files_properties("{root.as_posix()}/tests/creative_flight_server_runtime_tests.cpp" "{root.as_posix()}/src/flight_session.cpp" PROPERTIES COMPILE_OPTIONS "/EHsc")
# Only the runtime TU needs SEH-aware cleanup of private native row pointers.
set_source_files_properties("{root.as_posix()}/src/creative_flight_server_runtime.cpp" PROPERTIES COMPILE_OPTIONS "/EHa")
target_compile_definitions(creative_flight_server_runtime_tests PRIVATE NOMINMAX)
add_executable(creative_flight_dispatch_bridge_tests "{root.as_posix()}/tests/creative_flight_dispatch_bridge_tests.cpp" "{root.as_posix()}/src/creative_flight_dispatch_bridge.asm" "{root.as_posix()}/tests/creative_flight_dispatch_fixture.asm")
target_include_directories(creative_flight_dispatch_bridge_tests PRIVATE "{root.as_posix()}/src")
target_compile_features(creative_flight_dispatch_bridge_tests PRIVATE cxx_std_20)
target_compile_options(creative_flight_dispatch_bridge_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
target_compile_definitions(creative_flight_dispatch_bridge_tests PRIVATE NOMINMAX)
enable_testing()
add_test(NAME creative_flight_dispatch_bridge COMMAND creative_flight_dispatch_bridge_tests)
add_test(NAME creative_flight_server_runtime COMMAND creative_flight_server_runtime_tests)
''')
    for command in ([str(cmake), "-S", str(source), "-B", str(binary), "-G", "Visual Studio 17 2022", "-A", "x64"],
                    [str(cmake), "--build", str(binary), "--config", "Release"],
                    [str(ctest), "--test-dir", str(binary), "-C", "Release", "--output-on-failure"],
                    [str(binary / "Release/creative_flight_server_runtime_tests.exe")],
                    [str(binary / "Release/creative_flight_dispatch_bridge_tests.exe")]):
        result=subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(result.stdout,end="")
        with (build/"test-output.log").open("a",encoding="utf-8") as log:
            log.write("$ "+repr(command)+"\n"+result.stdout+"\n")
        result.check_returncode()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    args = parser.parse_args()
    run(args.build_dir, args.cmake)


