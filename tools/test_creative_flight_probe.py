"""Build/run the bounded G input observer regressions in an isolated VS2022 tree.
Usage: python test_creative_flight_probe.py --build-dir E:/Build/gflight-probe --cmake PATH/TO/cmake.exe
The repository CMake and canonical packages are untouched. No game is executed.
"""
from pathlib import Path
import argparse
import subprocess


def run(build: Path, cmake: Path) -> None:
    repo = Path(__file__).resolve().parents[1]
    build = build.resolve()
    marker = build / ".creative-flight-probe-test"
    if build.exists() and any(build.iterdir()) and not marker.is_file():
        raise ValueError("refusing a nonempty build directory without this harness marker")
    if not cmake.is_file() or not (cmake.parent / "ctest.exe").is_file():
        raise ValueError("provide existing VS2022 CMake/CTest executable paths")
    if any(c in repo.as_posix() for c in '\"\n\r;'):
        raise ValueError("unsupported repository path characters")
    build.mkdir(parents=True, exist_ok=True)
    marker.write_text("bounded creative-flight observer tests\n", encoding="utf-8")
    source = build / "source"
    source.mkdir(exist_ok=True)
    cmake_source = f'''cmake_minimum_required(VERSION 3.20)
project(creative_flight_probe_harness LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(creative_flight_probe_tests "{repo.as_posix()}/tests/creative_flight_probe_tests.cpp" "{repo.as_posix()}/src/flight_session.cpp")
target_include_directories(creative_flight_probe_tests PRIVATE "{repo.as_posix()}/src")
target_compile_definitions(creative_flight_probe_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(creative_flight_probe_tests PRIVATE /W4 /WX /EHsc)
enable_testing()
add_test(NAME creative_flight_probe COMMAND creative_flight_probe_tests)
'''
    (source / "CMakeLists.txt").write_text(cmake_source, encoding="utf-8")
    binary = build / "binary"
    commands = [
        [str(cmake), "-S", str(source), "-B", str(binary), "-G", "Visual Studio 17 2022", "-A", "x64"],
        [str(cmake), "--build", str(binary), "--config", "Release"],
        [str(cmake.parent / "ctest.exe"), "--test-dir", str(binary), "-C", "Release", "--output-on-failure"],
        [str(binary / "Release" / "creative_flight_probe_tests.exe")],
    ]
    for command in commands:
        subprocess.run(command, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    args = parser.parse_args()
    run(args.build_dir, args.cmake)
