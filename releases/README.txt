Radiant Core Releases
=====================

Release artifacts for Radiant Core.

Latest Release: v2.3.0
----------------------
- See doc/release-notes/release-notes-2.3.0.md for full details
- v2.3.0/RELEASE_NOTES.md                               - Full release notes
- v2.3.0/radiant-core-linux-x64-v2.3.0.tar.gz           - Linux x86_64 CLI binaries (Ubuntu 22.04+)
- v2.3.0/radiant-core-macos-arm64-v2.3.0.tar.gz         - macOS ARM64 CLI binaries
- v2.3.0/radiant-core-gui-macos-arm64-v2.3.0.zip        - macOS ARM64 GUI app bundle
- v2.3.0/radiant-core-docker-v2.3.0.tar.gz              - Docker runtime image (amd64, Ubuntu 22.04)
- v2.3.0/Windows/RadiantCore.exe                         - Classic Qt GUI wallet (requires DLLs)
- v2.3.0/Windows/RadiantCoreNode+Wallet-v.2.3.0.exe     - Standalone Node+Wallet GUI (no DLLs needed)
- v2.3.0/Windows/radiant-core-windows-x64.zip            - All-in-one archive (all exes + all DLLs)
- v2.3.0/Windows/radiant-core-windows-x64.sha256         - Windows SHA-256 checksums
- v2.3.0/*.sha256                                        - SHA-256 checksums

v2.1.0 Release:
-----------------
- V2 Hard Fork release with 6 new/re-enabled opcodes (OP_BLAKE3, OP_K12, OP_LSHIFT, OP_RSHIFT, OP_2MUL, OP_2DIV)
- Activation height: block 410,000 (mainnet & testnet3)
- v2.1.0/RELEASE_NOTES.md                               - Full release notes
- v2.1.0/radiant-core-linux-x64-v2.1.0.tar.gz           - Linux x86_64 CLI binaries (Ubuntu 22.04+)
- v2.1.0/radiant-core-macos-arm64-v2.1.0.tar.gz         - macOS ARM64 CLI binaries
- v2.1.0/radiant-core-gui-macos-arm64-v2.1.0.zip        - macOS ARM64 GUI app bundle
- v2.1.0/radiant-core-docker-v2.1.0.tar.gz              - Docker runtime image (amd64, Ubuntu 22.04)
- v2.1.0/Windows/RadiantCore.exe                         - Classic Qt GUI wallet (requires DLLs)
- v2.1.0/Windows/RadiantCoreNode+Wallet-v.2.1.0.exe     - Standalone Node+Wallet GUI (no DLLs needed)
- v2.1.0/Windows/radiant-core-windows-x64.zip            - All-in-one archive (all exes + all DLLs)
- v2.1.0/Windows/radiant-core-windows-x64.sha256         - Windows SHA-256 checksums
- v2.1.0/*.sha256                                        - SHA-256 checksums

v2.0.1 Release:
-----------------
- v2.0.1/Windows/      - Windows x86_64 binaries + GUI (built from GitHub source)

v2.0.0 Releases:
-----------------
- Linux/                - Linux x86_64 binaries
- Mac - Apple Silicon/  - macOS ARM64 (Apple Silicon) binaries
- Windows/              - Windows x86_64 binaries + GUI (v2.0.0)
- Docker/               - Docker image tarball
- Radiant-Core-GUI-2.0.0.dmg - macOS GUI Application (node + wallet)

All releases include:
- radiantd      - Node daemon (full blockchain node, validates and relays transactions)
- radiant-cli   - CLI client (command-line interface to interact with a running node)
- radiant-tx    - Transaction utility (create, sign, and inspect raw transactions offline)
- Wallet support enabled

Build Configuration:
--------------------
All releases are built with the following recommended options:

  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_WALLET=ON \
    -DBUILD_RADIANT_DAEMON=ON \
    -DBUILD_RADIANT_CLI=ON \
    -DBUILD_RADIANT_TX=ON \
    -DBUILD_RADIANT_ZMQ=ON \
    -DENABLE_HARDENING=ON \
    -DENABLE_UPNP=ON \
    ..

| Option               | Value | Description                    |
|----------------------|-------|--------------------------------|
| BUILD_RADIANT_WALLET | ON    | Wallet functionality (GUI)     |
| BUILD_RADIANT_DAEMON | ON    | Main node binary               |
| BUILD_RADIANT_CLI    | ON    | Command-line interface         |
| BUILD_RADIANT_TX     | ON    | Transaction utility            |
| BUILD_RADIANT_ZMQ    | ON    | Real-time notifications        |
| ENABLE_HARDENING     | ON    | Security hardening             |
| ENABLE_UPNP          | ON    | Automatic port forwarding      |

Build Scripts:
--------------
To rebuild releases with the same configuration:

  ./build-mac-arm64.sh   # macOS Apple Silicon
  ./build-linux-x64.sh   # Linux x86_64 (run on Linux)
  ./build-docker.sh      # Docker image

Linux x86_64:
-------------
  # Install runtime dependencies (Ubuntu 22.04)
  sudo apt-get install libboost-chrono1.74.0 libboost-filesystem1.74.0 \
    libboost-thread1.74.0 libevent-2.1-7 libevent-pthreads-2.1-7 \
    libssl3 libdb5.3++ libminiupnpc17 libzmq5

  # Extract and run
  cd Linux
  tar -xzf radiant-core-linux-x64.tar.gz
  cd radiant-core-linux-x64
  ./radiantd --version
  ./radiantd -server -txindex=1

macOS (Apple Silicon):
----------------------

  OPTION 1: GUI Application (Recommended for most users)
  -------------------------------------------------------
  Download Radiant-Core-GUI-2.0.0.dmg, open it, and drag to Applications.
  Double-click to launch - includes node and wallet in one app.
  
  If blocked by Gatekeeper:
    xattr -rd com.apple.quarantine /Applications/Radiant\ Core.app

  OPTION 2: Command-Line Binaries
  --------------------------------
  cd "Mac - Apple Silicon"
  unzip radiant-core-macos-arm64.zip
  cd radiant-core-macos-arm64
  
  # Remove quarantine (required for downloaded files)
  xattr -rd com.apple.quarantine .
  
  ./radiantd --version
  ./radiantd -server -txindex=1

Docker:
-------
  # Load the image (x86_64/amd64 architecture)
  docker load < Docker/radiant-core-docker-v2.0.0.tar.gz
  
  # Run with wallet support
  docker run -d --name radiant-node \
    -p 7332:7332 -p 7333:7333 \
    -v radiant-data:/home/radiant/.radiant \
    radiant-core:v2.0.0-amd64
  
  # Check status
  docker exec radiant-node radiant-cli getblockchaininfo
  
  # View wallet commands
  docker exec radiant-node radiant-cli help | grep -A50 "== Wallet =="

Windows v2.1.0 (x86_64):
------------------------
  cd v2.1.0/Windows
  .\radiantd.exe --version
  .\radiantd.exe -server -txindex=1

  CLI Executables:
  - radiantd.exe          - Node daemon (full blockchain node, validates and relays transactions)
  - radiant-cli.exe       - CLI client (command-line interface to interact with a running node)
  - radiant-tx.exe        - Transaction utility (create, sign, and inspect raw transactions offline)

  GUI Applications (two options):
  - RadiantCore.exe                    - Classic Qt GUI wallet and node manager.
                                         Compiled from C++ source (radiant-qt).
                                         Requires Qt5 + ICU + MinGW DLLs in the same folder.
  - RadiantCoreNode+Wallet-v.2.1.0.exe - Standalone Node+Wallet GUI (Python/web-based).
                                         Single-file executable — no DLLs required.
                                         Launches a browser-based interface at http://127.0.0.1:8765
                                         with one-click node control, built-in wallet, and BIP39 seed
                                         phrase backup.

  radiant-core-windows-x64.zip:
    All-in-one archive for developers and end users. Contains all five
    executables above plus every required DLL (Qt5, ICU, MinGW runtime,
    BerkeleyDB, HarfBuzz, FreeType, zlib, etc.) and the Qt5 platforms
    plugin (platforms/qwindows.dll). Extract and run — no additional
    dependencies needed.

  radiant-core-windows-x64.sha256:
    SHA-256 checksums for the executables, key DLLs, and the zip archive.

  Build: MSYS2 MinGW64, GCC 15.2.0, CMake, Make (CLI + Qt GUI)
         PyInstaller 6.18.0, Python 3.14 (Node+Wallet GUI)
  Source: https://github.com/Radiant-Core/Radiant-Core (main branch)

Windows v2.0.1 (x86_64):
------------------------
  cd v2.0.1/Windows
  .\radiantd.exe --version
  .\radiantd.exe -server -txindex=1

  Files included:
  - radiantd.exe          - Node daemon (v2.0.1-bc76dbc)
  - radiant-cli.exe       - Command-line interface
  - radiant-tx.exe        - Transaction utility
  - RadiantCore.exe       - GUI application (optional)
  - radiant-core-windows-x64.zip - All-in-one archive
  - Required DLLs (statically linked OpenSSL, libevent, ZeroMQ, Boost):
    - libdb_cxx-6.2.dll (BerkeleyDB)
    - libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll (MinGW runtime)

  Build: MSYS2 MinGW64, GCC 15.2.0, CMake, Ninja
  Source: https://github.com/Radiant-Core/Radiant-Core (main branch)

Windows v2.0.0 (x86_64):
------------------------
  cd Windows
  (See v2.0.0 files - 14 DLLs, dynamically linked)

Windows GUI Options:
--------------------
  OPTION 1: RadiantCoreNode+Wallet (Recommended for most users)
  -------------------------------------------------------------
  1. Double-click RadiantCoreNode+Wallet-v.2.1.0.exe
  2. A browser-based GUI opens at http://127.0.0.1:8765
  3. One-click node start/stop, built-in wallet, BIP39 seed phrase backup
  4. Single file — no DLLs or installation required

  OPTION 2: RadiantCore Qt GUI (Classic desktop wallet)
  -----------------------------------------------------
  1. Extract radiant-core-windows-x64.zip to a folder
  2. Double-click RadiantCore.exe
  3. Native Qt desktop wallet and node manager
  4. Requires all DLLs in the same folder (included in the zip)

  OPTION 3: CLI only (for developers/servers)
  -------------------------------------------
  1. Extract radiant-core-windows-x64.zip to a folder
  2. Run radiantd.exe and radiant-cli.exe from the command line

Windows (via WSL2 - for developers):
------------------------------------
  1. Install WSL2: wsl --install -d Ubuntu-24.04
  2. Use the Linux release inside WSL2

Verify Checksums:
-----------------
Each release includes a .sha256 file. Verify with:
  shasum -a 256 -c <release>.sha256

Ports:
------
- Mainnet: 7332 (RPC), 7333 (P2P)
- Testnet: 27332 (RPC), 27333 (P2P)

For more information: https://radiantblockchain.org
Source: https://github.com/Radiant-Core/Radiant-Core
