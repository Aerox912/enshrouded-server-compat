"""Build/run the isolated G flight movement policy tests."""
import argparse
import subprocess
from pathlib import Path


def run(build: Path, cmake: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    ctest = cmake.with_name("ctest.exe")
    if not cmake.is_file() or not ctest.is_file():
        raise ValueError("provide the existing native Windows CMake/CTest toolchain")
    marker = build / ".gflight-movement-test-root"
    if build.exists() and any(build.iterdir()) and (not marker.is_file() or marker.read_text() != str(root)):
        raise ValueError("nonempty build directory does not belong to this isolated harness")
    build.mkdir(parents=True, exist_ok=True)
    marker.write_text(str(root))
    source, binary = build / "source", build / "binary"
    source.mkdir(exist_ok=True)
    (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.24)
project(gflight_movement_test LANGUAGES CXX)
add_executable(creative_flight_movement_tests "{root.as_posix()}/tests/creative_flight_movement_tests.cpp" "{root.as_posix()}/src/flight_session.cpp")
target_include_directories(creative_flight_movement_tests PRIVATE "{root.as_posix()}/src")
target_compile_features(creative_flight_movement_tests PRIVATE cxx_std_20)
target_compile_options(creative_flight_movement_tests PRIVATE /W4 /WX /EHsc)
target_compile_definitions(creative_flight_movement_tests PRIVATE NOMINMAX)
enable_testing()
add_test(NAME creative_flight_movement COMMAND creative_flight_movement_tests)
''')
    for command in ([str(cmake), "-S", str(source), "-B", str(binary), "-G", "Visual Studio 17 2022", "-A", "x64"],
                    [str(cmake), "--build", str(binary), "--config", "Release"],
                    [str(ctest), "--test-dir", str(binary), "-C", "Release", "--output-on-failure"],
                    [str(binary / "Release/creative_flight_movement_tests.exe")]):
        subprocess.run(command, check=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    args = parser.parse_args()
    run(args.build_dir, args.cmake)
