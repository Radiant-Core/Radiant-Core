# Radiant Core GUI

A simple browser-based GUI for running a Radiant Core node. Designed for non-technical users to easily start and manage their node.

## Download

### macOS App (Recommended)

Download the standalone macOS application - no dependencies required:

| Platform | Download | Size |
|----------|----------|------|
| **macOS (Apple Silicon/Intel)** | [Radiant-Core-GUI-3.1.0.dmg](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/Radiant-Core-GUI-3.1.0.dmg) | ~19 MB |
| **Windows (standalone)** | [RadiantCoreNode+Wallet-v.3.1.0.exe](../releases/v3.1.0/Windows/RadiantCoreNode+Wallet-v.3.1.0.exe) | ~9.2 MB |
| **Windows (Qt classic)** | [RadiantCore.exe](../releases/v3.1.0/Windows/RadiantCore.exe) | ~30 MB |
| **Windows (all-in-one)** | [radiant-core-windows-x64.zip](../releases/v3.1.0/Windows/radiant-core-windows-x64.zip) | ~65 MB |
| **Linux (x86_64)** | [radiant-core-gui-linux-x64-v3.1.0.tar.gz](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-gui-linux-x64-v3.1.0.tar.gz) | ~15 MB |

**Quick Install (macOS DMG):**
1. Download the DMG file
2. Open the DMG and drag "Radiant Core" to Applications
3. First launch: Right-click the app → Open (to bypass Gatekeeper)
   On newer software it may not want to open the unsigned (Apple approved) app. Go to Settings > Privacy and Security > Approve running the Radiant Core App 
4. The app includes all node binaries - no additional downloads needed

**If macOS blocks the app:**
```bash
xattr -rd com.apple.quarantine /Applications/Radiant\ Core.app
```

**Windows — Two GUI Options:**

**Option A: RadiantCoreNode+Wallet (Recommended)**
1. Download `RadiantCoreNode+Wallet-v.3.1.0.exe` (~9.2 MB)
2. Double-click to run — no DLLs or installation needed
3. A browser-based GUI opens at `http://127.0.0.1:8765`
4. Includes one-click node control, built-in wallet, and BIP39 seed phrase backup

**Option B: RadiantCore Qt GUI (Classic desktop wallet)**
1. Download and extract `radiant-core-windows-x64.zip` (~65 MB)
2. Double-click `RadiantCore.exe`
3. Native Qt desktop wallet and node manager
4. All required DLLs (Qt5, ICU, MinGW runtime, etc.) are included in the zip

### Portable Packages (All Platforms)

For users who prefer a portable installation or Linux:

| Platform | Download | Size |
|----------|----------|------|
| **macOS (Apple Silicon)** | [radiant-core-gui-macos-arm64-v3.1.0.zip](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-gui-macos-arm64-v3.1.0.zip) | ~15 MB |
| **Linux (x86_64)** | [radiant-core-gui-linux-x64-v3.1.0.tar.gz](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-gui-linux-x64-v3.1.0.tar.gz) | ~15 MB |

**macOS Portable:**
```bash
# Download and extract
curl -LO https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-gui-macos-arm64-v3.1.0.zip
unzip radiant-core-gui-macos-arm64-v3.1.0.zip
cd radiant-core-gui-macos-arm64-v3.1.0

# Remove quarantine (required for downloaded apps)
xattr -rd com.apple.quarantine .

# Launch
./start-gui.command
```

**Linux:**
```bash
curl -LO https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-gui-linux-x64-v3.1.0.tar.gz
tar xzf radiant-core-gui-linux-x64-v3.1.0.tar.gz
cd radiant-core-gui-linux-x64-v3.1.0
./start-gui.sh
```

The GUI opens automatically in your browser at **http://127.0.0.1:8765**

## Features

- **One-click start/stop** - Start and stop your node with a single button click
- **Real-time status** - See sync progress, block count, and peer count in real-time
- **Network selection** - Easily switch between mainnet, testnet, and regtest
- **Pruning support** - Enable pruning to save disk space
- **Log output** - View node activity in the built-in log panel
- **Node info** - View detailed node information with one click
- **No dependencies** - Uses only Python standard library, works everywhere
- **Modern UI** - Clean interface with light/dark mode toggle
- **Wallet Integration** - Send/receive RXD, view balances and transactions (requires wallet-enabled build)
- **Auto-start** - Optionally start the node automatically when GUI launches
- **Auto-download binaries** - Automatically detect your platform and download pre-built binaries
- **Wallet Backup & Restore** - Export/import private keys and seed phrases for wallet recovery

## Architecture

### How the GUI Interfaces with the Node

The GUI communicates with the Radiant node through **RPC (Remote Procedure Call)**:

```
┌─────────────┐     HTTP      ┌─────────────────┐    RPC     ┌───────────┐
│   Browser   │ ◄──────────► │  Python Backend  │ ◄────────► │  radiantd │
│   (GUI)     │   Port 8765   │ (radiant_node_   │  via CLI   │   (node)  │
└─────────────┘               │    web.py)       │            └───────────┘
                              └─────────────────┘
```

**Data Flow:**
1. Browser sends requests to Python backend on `http://127.0.0.1:8765`
2. Python backend executes `radiant-cli` commands to communicate with the node
3. Node responds via RPC, Python parses and returns JSON to browser

**Key RPC Commands Used:**
| Command | Purpose |
|---------|----------|
| `getblockchaininfo` | Block height, sync progress, chain name |
| `getnetworkinfo` | Peer count, version info |
| `getwalletinfo` | Wallet balance, status |
| `getnewaddress` | Generate new receiving address |
| `sendtoaddress` | Send RXD transactions |
| `listtransactions` | Transaction history |

**Requirements for RPC:**
- Node must have `server=1` in config (the GUI sets this automatically)
- RPC is local-only by default for security

### Wallet Support

The Wallet tab requires the node to be compiled with wallet support. If you see "Wallet not available" in the GUI:

**Option 1: Build from source with wallet enabled**
```bash
cd radiant-core
mkdir -p build && cd build
cmake -DBUILD_RADIANT_WALLET=ON ..
make -j$(nproc)
```

**Option 2: Use pre-built binaries with wallet support**

Download wallet-enabled binaries from the [GitHub Releases page](https://github.com/Radiant-Core/Radiant-Core/releases/tag/v3.1.0):

| Platform | Download |
|----------|----------|
| macOS (Apple Silicon) | [radiant-core-macos-arm64.tar.gz](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-macos-arm64.tar.gz) |
| Linux (x86_64) | [radiant-core-linux-x64.tar.gz](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-linux-x64.tar.gz) |
| Docker (x86_64) | [radiant-core-docker-v3.1.0.tar.gz](https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-docker-v3.1.0.tar.gz) |

**Quick setup (macOS):**
```bash
# Download and extract
curl -LO https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-macos-arm64.tar.gz
tar xzf radiant-core-macos-arm64.tar.gz

# Remove quarantine (required for downloaded binaries)
xattr -rd com.apple.quarantine radiant-core-macos-arm64

# Run the GUI
cd radiant-core-macos-arm64
python3 ../gui/radiant_node_web.py
```

**Quick setup (Linux):**
```bash
curl -LO https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.1.0/radiant-core-linux-x64.tar.gz
tar xzf radiant-core-linux-x64.tar.gz
cd radiant-core-linux-x64
./radiantd -server -txindex=1
```

**Note:** When the GUI starts a node, it automatically enables wallet functionality with the `-disablewallet=0` flag.

### Wallet Backup & Restore

The GUI provides several ways to backup and restore your wallet:

#### Backup Options

| Method | Description | Use Case |
|--------|-------------|----------|
| **Backup Wallet File** | Creates a copy of `wallet.dat` | Full backup of all keys and transactions |
| **Export Private Key** | Exports WIF key for a specific address | Backup individual addresses |
| **Export Seed Phrase** | 12/24-word mnemonic phrase | Human-readable backup, easy to write down |

#### Restore Options

| Method | Description |
|--------|-------------|
| **Import Private Key** | Import a WIF-format private key |
| **Import Seed Phrase** | Restore wallet from 12/24-word mnemonic |

#### Using Seed Phrases (Recommended)

Seed phrases are the safest way to backup your wallet:

1. Go to **Wallet** tab → **Backup & Restore** section
2. Click **Generate Seed Phrase** to create a new 12-word phrase
3. **Write it down on paper** - never store digitally!
4. To restore: Enter your seed phrase and click **Import Seed Phrase**

⚠️ **Security Warning:**
- Never share your seed phrase or private keys
- Anyone with access can steal your funds
- Store backups in a secure, offline location

### Interfacing with an Existing Node

If you have a node already running (started outside the GUI), the GUI can interface with it as long as:
- The node was started with RPC enabled (`server=1`)
- The `radiant-cli` binary is accessible
- The node is running on the expected network (mainnet/testnet/regtest)

The GUI will detect the running node and display its status.

## Requirements

- **Python 3.6+** - Usually pre-installed on macOS and Linux
- **Radiant Node binaries** - Either build from source or download pre-built binaries
- **Web browser** - Any modern browser (Chrome, Firefox, Safari, Brave)

### Installing Python

#### macOS
Python 3 is usually pre-installed. If not:
```bash
brew install python3
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt install python3
```

## Quick Start

### Windows (Recommended)

**Option A: RadiantCoreNode+Wallet (standalone, no DLLs needed)**
1. Double-click `RadiantCoreNode+Wallet-v.3.1.0.exe`
2. The GUI opens automatically in your browser at `http://127.0.0.1:8765`
3. One-click node control, built-in wallet, BIP39 seed phrase backup

**Option B: RadiantCore Qt GUI (classic desktop wallet)**
1. Extract `radiant-core-windows-x64.zip` to a folder
2. Double-click `RadiantCore.exe`
3. Requires all DLLs in the same folder (included in the zip)

**Files included in radiant-core-windows-x64.zip:**
- `RadiantCoreNode+Wallet-v.3.1.0.exe` - Standalone Node+Wallet GUI (no DLLs needed)
- `RadiantCore.exe` - Classic Qt GUI wallet (requires DLLs)
- `radiantd.exe` - The Radiant node daemon
- `radiant-cli.exe` - Command-line interface
- `radiant-tx.exe` - Transaction utility
- All required DLLs (Qt5, ICU, MinGW runtime, BerkeleyDB, etc.)

### macOS
1. Double-click `run_node_gui.command`
2. If prompted, right-click → Open to bypass Gatekeeper

### Linux
```bash
chmod +x run_node_gui.sh
./run_node_gui.sh
```

### Command Line (All Platforms)
```bash
python3 radiant_node_web.py
```

## Usage

### Starting the Node

1. Launch the GUI application
2. (Optional) Adjust settings:
   - **Network**: Choose mainnet, testnet, or regtest
   - **Data Dir**: Where blockchain data is stored
   - **Pruning**: Enable to save disk space (minimum 550 MB)
3. Click **▶ Start Node**
4. Watch the log output for progress

### Stopping the Node

1. Click **■ Stop Node**
2. Wait for graceful shutdown
3. The status indicator will turn gray when stopped

### Viewing Node Info

Click **ℹ Node Info** to see:
- Blockchain sync status
- Network information
- Connected peers

## Settings

Settings are automatically saved to `node_settings.json` in the gui folder.

### Network Options

| Network | Description |
|---------|-------------|
| mainnet | Main Radiant network (real RXD) |
| testnet | Test network (test RXD, no value) |
| regtest | Local regression testing network |

### Pruning

Pruning reduces disk usage by deleting old block data. The blockchain will still be fully validated but you won't be able to serve old blocks to other nodes.

- **Minimum**: 550 MB
- **Recommended**: 1000+ MB for better performance

## Troubleshooting

### "Could not find radiantd binary"

The GUI looks for the node binary in these locations:
1. `build/src/radiantd` (after building)
2. `src/radiantd`
3. `/usr/local/bin/radiantd`
4. `/usr/bin/radiantd`

**Solutions:**
- Build the node from source: See [INSTALL.md](../INSTALL.md)
- Or download pre-built binaries from [GitHub Releases](https://github.com/Radiant-Core/Radiant-Core/releases/tag/v3.1.0)
- Or use the GUI's built-in **Download Binaries** feature (auto-detects your platform)

### Node won't start

1. Check the log output for error messages
2. Ensure the data directory exists and is writable
3. Check if another node is already running
4. Verify you have enough disk space

### GUI looks different on my system

The GUI uses your system's native theme. Appearance may vary between:
- macOS (Aqua theme)
- Windows (Windows theme)
- Linux (depends on desktop environment)

## Getting Binaries

### Option 1: Download Pre-built (Recommended)

The GUI can automatically download the correct binaries for your platform. Just click **Download Binaries** when prompted.

Or download manually from [GitHub Releases](https://github.com/Radiant-Core/Radiant-Core/releases/tag/v3.1.0).

### Option 2: Build from Source

```bash
# Install dependencies (macOS)
brew install cmake boost openssl libevent berkeley-db@4

# Build with wallet support
mkdir build && cd build
cmake -DBUILD_RADIANT_WALLET=ON ..
make -j$(sysctl -n hw.ncpu)
```

See [INSTALL.md](../INSTALL.md) for detailed build instructions.

## Support

- Website: [radiantblockchain.org](https://radiantblockchain.org)
- Documentation: [doc/](../doc/)

## License

This software is released under the MIT License. See [COPYING](../COPYING) for details.
