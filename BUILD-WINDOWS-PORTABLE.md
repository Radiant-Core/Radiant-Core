# Portable Windows Build Instructions for Radiant Core

## Prerequisites
- Windows 10/11
- Visual Studio 2019/2022 with C++ development tools OR MinGW-w64
- CMake 3.22+
- Python 3.6+
- Git

## Option 1: Using Visual Studio (Recommended for distribution)

### Step 1: Install Dependencies
1. Install Visual Studio 2019/2022 with C++ development tools
2. Install vcpkg: `git clone https://github.com/Microsoft/vcpkg.git`
3. Install vcpkg packages:
   ```
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg install openssl:x64-windows
   .\vcpkg install boost:x64-windows
   .\vcpkg install libevent:x64-windows
   ```

### Step 2: Build
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake -DBUILD_RADIANT_WALLET=OFF -DBUILD_RADIANT_ZMQ=OFF -DENABLE_UPNP=OFF
cmake --build . --config Release
```

## Option 2: Using MinGW-w64 (Current setup, made portable)

### Step 1: Install Dependencies
1. Install MSYS2: https://www.msys2.org/
2. In MSYS2, install:
   ```
   pacman -S mingw-w64-x86_64-gcc
   pacman -S mingw-w64-x86_64-cmake
   pacman -S mingw-w64-x86_64-openssl
   pacman -S mingw-w64-x86_64-boost
   pacman -S mingw-w64-x86_64-libevent
   pacman -S mingw-w64-x86_64-python
   ```

### Step 2: Build (Portable)
```cmd
# Make sure to use the MSYS2 MinGW shell
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DBUILD_RADIANT_WALLET=OFF -DBUILD_RADIANT_ZMQ=OFF -DENABLE_UPNP=OFF
mingw32-make -j$(nproc)
```

## Creating a Portable Distribution

### Step 1: Copy Required DLLs
After building, copy these DLLs to the output directory:
- libssl-3-x64.dll
- libcrypto-3-x64.dll
- libevent-2-1.dll
- libgcc_s_seh-1.dll
- libstdc++-6.dll
- libwinpthread-1.dll

### Step 2: Create Installer
Use NSIS or WiX to create an installer that includes:
- All executables (radiantd.exe, radiant-cli.exe, radiant-tx.exe)
- Required DLLs
- Configuration files
- Documentation

## Automated Build Script

For automated building, use the provided scripts:
- `build-portable-windows.bat` - Automated Windows build
- `create-installer.nsi` - NSIS installer script
