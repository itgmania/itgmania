cd %~dp0..

cmake -B Build\release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=On || exit /b
cmake --build Build\release --config Release -j %NUMBER_OF_PROCESSORS% || exit /b
cmake --build Build\release --config Release --target package || exit /b
cmake -B Build\release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=Off || exit /b
cmake --build Build\release --config Release --target package || exit /b
