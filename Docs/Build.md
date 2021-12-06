# Build Instructions

## Preparations
```
git clone --recursive https://github.com/SpriteOvO/AirPodsDesktop.git
cd AirPodsDesktop
mkdir Build
cd Build
```
The current path will be named ___BuildPath___ in the rest of this document.

1. Download and install [CMake](https://cmake.org/download/) (>= v3.20) if you didn't have it.

2. Choose a target platform to continue:
    - [Windows](#windows)

## Windows

1. Download and install __Visual Studio 2019__ if you didn't have it.

2. Clone and bootstrap [vcpkg](https://github.com/microsoft/vcpkg#quick-start-windows) if you didn't have it.

3. Download and install [Qt 5.15.2](https://www.qt.io/download-qt-installer). (Minimum required components are only `MSVC 2019 32-bit`)  
Set the installed Qt directory to the `PATH` environment variable, or pass it to the CMake option later.

4. Download and install [NSIS](https://sourceforge.net/projects/nsis/files/latest/download). (Optional, not required if you do not generate installer)

5. Open __Powershell__, go to ___BuildPath___.  
Modify the following arguments according to your needs and run it.

    ```
    cmake -G "Visual Studio 16 2019" -A Win32 -DCMAKE_BUILD_TYPE=<Debug|RelWithDebInfo> -DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake ../
    cmake --build . --config <Debug|RelWithDebInfo>
    ls ./Binary
    ```

    - Note that if you have not just added the Qt directory to the `PATH` environment variable, you need to pass it to the `CMAKE_PREFIX_PATH` option in the first line this way `-DCMAKE_PREFIX_PATH=path\to\Qt\5.15.2\msvc2019`.
    - See the [CMakeLists.txt](/CMakeLists.txt) `Build options` section for more options.
