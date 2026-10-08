#!/bin/sh
set -eu
wine=/usr/lib/wine/wine64
"$wine" --version
"$wine" /tests/flight_session_tests.exe
"$wine" /tests/flight_identity_tests.exe
"$wine" /tests/flight_tests.exe 'Z:\server\enshrouded_server.exe'
