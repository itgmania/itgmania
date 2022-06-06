cd %~dp0..

cmake -B Build\release -DWITH_FULL_RELEASE=On || exit /b
cmake --build Build\release --config Release -j %NUMBER_OF_PROCESSORS% || exit /b
cmake --build Build\release --config Release --target package || exit /b
