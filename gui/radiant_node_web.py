#!/usr/bin/env python3
"""
Radiant Core Node - Simple browser-based interface for running a Radiant node
Designed for non-technical users to easily start and manage their node.
Uses only Python standard library - no external dependencies required.
"""

import argparse
import hmac
import http.server
import json
import os
import platform
import re
import subprocess
import threading
import time
import webbrowser
import tarfile
import zipfile
import hashlib
from pathlib import Path
from urllib.parse import parse_qs, urlparse
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError
import socketserver
import signal
import sys
import shutil

# pywebview is optional - only needed for windowed mode on macOS
WEBVIEW_AVAILABLE = False
try:
    import webview
    WEBVIEW_AVAILABLE = True
except ImportError:
    pass

# Import BIP39 mnemonic support
try:
    from bip39 import (generate_mnemonic, validate_mnemonic, mnemonic_to_wif,
                        derive_path, derive_bip44_key, VALID_WORD_COUNTS)
    BIP39_AVAILABLE = True
except ImportError:
    BIP39_AVAILABLE = False
    VALID_WORD_COUNTS = (12, 15, 18, 21, 24)

# GitHub release configuration
GITHUB_RELEASE_URL = "https://github.com/Radiant-Core/Radiant-Core/releases/download/v3.0.0"
# C4 Security: SHA-256 hashes for release asset verification
# These must be updated with each release by running: sha256sum <asset_file>
RELEASE_ASSETS = {
    "darwin_arm64": {
        "filename": "radiant-core-macos-arm64.zip",
        "folder": "radiant-core-macos-arm64",
        "display": "macOS (Apple Silicon)",
        # C4: SHA-256 hash - MUST be updated at release time
        "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE",
    },
    "darwin_x86_64": {
        "filename": "radiant-core-macos-arm64.zip",  # Use ARM64 for now, x64 not available
        "folder": "radiant-core-macos-arm64",
        "display": "macOS (Intel) - Using ARM64 binary via Rosetta",
        "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE",
    },
    "linux_x86_64": {
        "filename": "radiant-core-linux-x64.tar.gz",
        "folder": "radiant-core-linux-x64",
        "display": "Linux (x86_64)",
        "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE",
    },
    "linux_aarch64": {
        "filename": "radiant-core-linux-x64.tar.gz",  # ARM Linux not available yet
        "folder": "radiant-core-linux-x64",
        "display": "Linux (ARM64) - x64 binary (requires emulation)",
        "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE",
    },
    "windows_x64": {
        "filename": "radiant-core-windows-x64.zip",
        "folder": "radiant-core-windows-x64",
        "display": "Windows (x64)",
        # N1: empty hash is rejected (fail-closed) until configured at release time
        "sha256": "",
    },
}


def _release_hash_configured(asset):
    """N1: Return True only if the asset carries a real, usable SHA-256 hash.

    Fail-closed: a missing key, an empty value, or a PLACEHOLDER value means
    the release hash has not been pinned yet and the binary MUST NOT be run.
    """
    h = asset.get("sha256", "")
    if not h or h.startswith("PLACEHOLDER"):
        return False
    return True


# New-4: Strict input validation for values that are passed to CLI subprocess
# calls. The goal is to prevent argv-flag smuggling (a value starting with "-"
# being parsed by radiant-cli as an option) and to reject obviously malformed
# inputs before they ever reach the subprocess.
_ADDRESS_RE = re.compile(r'^[1-9A-HJ-NP-Za-km-z]{25,80}$')  # base58 address
_LABEL_RE = re.compile(r'^[\w \-]{0,100}$')                 # wallet label


def _reject_flag(value):
    """Return True if the value would be interpreted as a CLI flag (argv smuggling)."""
    return isinstance(value, str) and value.startswith('-')


def _validate_address(address):
    """Validate a base58 address. Returns (ok, error)."""
    if not isinstance(address, str) or _reject_flag(address):
        return False, "Invalid address"
    if not _ADDRESS_RE.match(address):
        return False, "Invalid address format"
    return True, None


def _validate_label(label):
    """Validate an optional wallet label. Returns (ok, error)."""
    if label is None:
        return True, None
    if not isinstance(label, str) or _reject_flag(label):
        return False, "Invalid label"
    if not _LABEL_RE.match(label):
        return False, "Invalid label format"
    return True, None


def _validate_backup_destination(destination):
    """Validate a wallet backup destination path. Returns (ok, error).

    Rejects argv-flag smuggling, NUL bytes, and confines the backup to a known
    safe directory (~/Documents/RadiantBackups) to avoid arbitrary file writes.
    """
    if not isinstance(destination, str) or not destination:
        return False, "Invalid backup destination"
    if _reject_flag(destination) or '\x00' in destination:
        return False, "Invalid backup destination"
    try:
        backup_dir = (Path.home() / "Documents" / "RadiantBackups").resolve()
        dest = Path(destination).resolve()
    except Exception:
        return False, "Invalid backup destination"
    # Confine to the known backups directory (no path traversal escape).
    if backup_dir not in dest.parents and dest != backup_dir:
        return False, "Backup destination must be inside the RadiantBackups directory"
    return True, None


def _validate_privkey(privkey):
    """Validate a WIF private key. Returns (ok, error).

    Only structural checks here -- the daemon does cryptographic validation.
    The key point is rejecting argv-flag smuggling; the secret itself is fed
    via stdin (H13) rather than argv.
    """
    if not isinstance(privkey, str) or not privkey:
        return False, "Private key is required"
    if _reject_flag(privkey):
        return False, "Invalid private key"
    if not _ADDRESS_RE.match(privkey):
        return False, "Invalid private key format"
    return True, None

# Fallback logo SVG (simple R icon) used when logo files can't be found
FALLBACK_LOGO_SVG = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <rect width="100" height="100" rx="20" fill="#1a1f2e"/>
  <text x="50" y="68" font-family="Arial,sans-serif" font-size="50" font-weight="bold" fill="#e6e6e6" text-anchor="middle">R</text>
</svg>'''


class DownloadManager:
    """Manages downloading and extracting Radiant Core binaries."""
    
    def __init__(self, base_path):
        self.base_path = Path(base_path)
        self.binaries_path = self.base_path / "binaries"
        self.download_progress = {"status": "idle", "percent": 0, "message": ""}
        self._download_thread = None
    
    def get_platform_key(self):
        """Detect current platform and return the asset key."""
        system = platform.system().lower()
        machine = platform.machine().lower()
        
        if system == "windows":
            return "windows_x64"
        elif system == "darwin":
            if machine in ("arm64", "aarch64"):
                return "darwin_arm64"
            return "darwin_x86_64"
        elif system == "linux":
            if machine in ("x86_64", "amd64"):
                return "linux_x86_64"
            elif machine in ("arm64", "aarch64"):
                return "linux_aarch64"
        return None
    
    def get_platform_info(self):
        """Get information about the detected platform and available download."""
        key = self.get_platform_key()
        if key and key in RELEASE_ASSETS:
            asset = RELEASE_ASSETS[key]
            return {
                "detected": True,
                "platform_key": key,
                "display_name": asset["display"],
                "filename": asset["filename"],
                "download_url": f"{GITHUB_RELEASE_URL}/{asset['filename']}",
                "installed": self._is_installed(key),
            }
        return {
            "detected": False,
            "platform_key": None,
            "display_name": f"Unknown ({platform.system()} {platform.machine()})",
            "available_platforms": [
                {"key": k, "display": v["display"]} 
                for k, v in RELEASE_ASSETS.items()
            ],
        }
    
    def _is_installed(self, platform_key):
        """Check if binaries are already installed for the given platform."""
        if platform_key not in RELEASE_ASSETS:
            return False
        folder = RELEASE_ASSETS[platform_key]["folder"]
        binary_name = "radiantd.exe" if platform.system() == "Windows" else "radiantd"
        binary_path = self.binaries_path / folder / binary_name
        if binary_path.exists():
            return True
        # Also check next to the running executable (release folder layout)
        if getattr(sys, 'frozen', False):
            exe_dir = Path(sys.executable).parent
            if (exe_dir / binary_name).exists():
                return True
        return False
    
    def get_binary_path(self, platform_key=None):
        """Get the path to installed binaries."""
        if platform_key is None:
            platform_key = self.get_platform_key()
        if platform_key and platform_key in RELEASE_ASSETS:
            folder = RELEASE_ASSETS[platform_key]["folder"]
            return self.binaries_path / folder
        return None
    
    def download_and_extract(self, platform_key=None):
        """Download and extract binaries for the specified platform."""
        if platform_key is None:
            platform_key = self.get_platform_key()
        
        if not platform_key or platform_key not in RELEASE_ASSETS:
            self.download_progress = {
                "status": "error",
                "percent": 0,
                "message": "Unsupported platform",
            }
            return False
        
        asset = RELEASE_ASSETS[platform_key]

        # N1: Fail-closed. Refuse to download/install any binary whose release
        # SHA-256 hash has not been pinned (missing/empty/PLACEHOLDER).
        if not _release_hash_configured(asset):
            self.download_progress = {
                "status": "error",
                "percent": 0,
                "message": "auto-download disabled: release hash not configured",
            }
            return False

        # Create binaries directory
        self.binaries_path.mkdir(parents=True, exist_ok=True)
        tar_path = self.binaries_path / asset["filename"]

        # Check for local file override (adjacent to executable or script)
        local_file = None
        if getattr(sys, 'frozen', False):
            # Running as PyInstaller EXE
            local_file = Path(sys.executable).parent / asset["filename"]
        else:
            # Running as script
            local_file = Path(__file__).parent / asset["filename"]
            
        use_local = False
        if local_file and local_file.exists():
             use_local = True
             self.download_progress = {
                 "status": "downloading",
                 "percent": 100,
                 "message": f"Installing from local file: {local_file.name}...",
             }
             try:
                 shutil.copy2(local_file, tar_path)
             except Exception as e:
                 self.download_progress = {
                     "status": "error",
                     "percent": 0,
                     "message": f"Local install failed: {str(e)}",
                 }
                 return False

        if not use_local:
            if "url" in asset:
                url = asset["url"]
            else:
                url = f"{GITHUB_RELEASE_URL}/{asset['filename']}"
            
            self.download_progress = {
                "status": "downloading",
                "percent": 0,
                "message": f"Downloading {asset['filename']}...",
            }
            
            try:
                # Download with progress
                req = Request(url, headers={"User-Agent": "RadiantCoreGUI/2.1.1"})
                with urlopen(req, timeout=60) as response:
                    total_size = int(response.headers.get("Content-Length", 0))
                    downloaded = 0
                    chunk_size = 65536
                    
                    with open(tar_path, "wb") as f:
                        while True:
                            chunk = response.read(chunk_size)
                            if not chunk:
                                break
                            f.write(chunk)
                            downloaded += len(chunk)
                            if total_size > 0:
                                percent = int((downloaded / total_size) * 100)
                                self.download_progress = {
                                    "status": "downloading",
                                    "percent": percent,
                                    "message": f"Downloading... {downloaded // 1024 // 1024}MB / {total_size // 1024 // 1024}MB",
                                }
            except HTTPError as e:
                self.download_progress = {
                    "status": "error",
                    "percent": 0,
                    "message": f"Download failed: HTTP {e.code}",
                }
                return False
            except URLError as e:
                self.download_progress = {
                    "status": "error",
                    "percent": 0,
                    "message": f"Network error: {e.reason}",
                }
                return False
            except Exception as e:
                self.download_progress = {
                    "status": "error",
                    "percent": 0,
                    "message": f"Error: {str(e)}",
                }
                return False

        # C4 Security: Verify SHA-256 hash before extraction
        self.download_progress = {
            "status": "verifying",
            "percent": 100,
            "message": "Verifying download...",
        }

        try:
            sha256_hash = hashlib.sha256()
            with open(tar_path, "rb") as f:
                for chunk in iter(lambda: f.read(65536), b""):
                    sha256_hash.update(chunk)
            computed_hash = sha256_hash.hexdigest()

            expected_hash = asset.get("sha256", "")
            # N1: Fail-closed. A missing/empty/PLACEHOLDER hash means the release
            # hash was never pinned -- refuse rather than running unverified code.
            if not _release_hash_configured(asset):
                tar_path.unlink()  # Delete the unverifiable file
                self.download_progress = {
                    "status": "error",
                    "percent": 0,
                    "message": "auto-download disabled: release hash not configured",
                }
                return False
            if computed_hash != expected_hash:
                tar_path.unlink()  # Delete the bad file
                self.download_progress = {
                    "status": "error",
                    "percent": 0,
                    "message": "Download verification failed: SHA-256 mismatch. File may be corrupted or tampered with.",
                }
                return False
        except Exception as e:
            self.download_progress = {
                "status": "error",
                "percent": 0,
                "message": f"Verification error: {str(e)}",
            }
            return False

        # Extract
        self.download_progress = {
            "status": "extracting",
            "percent": 100,
            "message": "Extracting files...",
        }

        try:
            # C4 Security: Extract with data filter to prevent path traversal attacks
            if asset["filename"].endswith(".zip"):
                with zipfile.ZipFile(tar_path, 'r') as zip_ref:
                    # C4: Use filter='data' if available (Python 3.12+) for path traversal protection
                    if hasattr(zip_ref, 'extractall') and callable(getattr(zipfile, 'ZipFile', None)):
                        try:
                            zip_ref.extractall(self.binaries_path, filter='data')
                        except TypeError:
                            # Python < 3.12 doesn't support filter parameter
                            zip_ref.extractall(self.binaries_path)
            else:
                with tarfile.open(tar_path, "r:gz") as tar:
                    # C4: Use filter='data' for path traversal protection (Python 3.12+)
                    try:
                        tar.extractall(path=self.binaries_path, filter='data')
                    except TypeError:
                        # Python < 3.12 doesn't support filter parameter
                        tar.extractall(path=self.binaries_path)

            # C4 Security: Removed xattr quarantine bypass that could allow malware to bypass Gatekeeper
            
            # Make binaries executable
            extract_path = self.binaries_path / asset["folder"]
            for binary in ["radiantd", "radiant-cli", "radiant-tx"]:
                bin_path = extract_path / binary
                if bin_path.exists():
                    bin_path.chmod(0o755)
            
            # Clean up archive file
            tar_path.unlink()
            
            self.download_progress = {
                "status": "complete",
                "percent": 100,
                "message": "Installation complete!",
                "binary_path": str(extract_path),
            }
            return True
            
        except Exception as e:
            self.download_progress = {
                "status": "error",
                "percent": 0,
                "message": f"Extraction failed: {str(e)}",
            }
            return False
    
    def start_download(self, platform_key=None):
        """Start download in background thread."""
        if self._download_thread and self._download_thread.is_alive():
            return {"success": False, "error": "Download already in progress"}
        
        self._download_thread = threading.Thread(
            target=self.download_and_extract,
            args=(platform_key,),
            daemon=True,
        )
        self._download_thread.start()
        return {"success": True, "message": "Download started"}
    
    def get_progress(self):
        """Get current download progress."""
        return self.download_progress


# Global state
node_process = None
node_output = []
MAX_LOG_LINES = 500

class NodeManager:
    def __init__(self):
        if getattr(sys, 'frozen', False):
            # Running as PyInstaller exe - use exe's directory for binaries
            self.exe_dir = Path(sys.executable).parent
            self.base_path = self.exe_dir
            # Use AppData for persistent settings
            appdata = Path(os.environ.get("APPDATA", "")) if platform.system() == "Windows" else Path.home()
            self.config_dir = appdata / "RadiantCore"
            self.config_dir.mkdir(parents=True, exist_ok=True)
            self.config_file = self.config_dir / "node_settings.json"
            # Use exe directory for download manager (downloads go next to exe)
            self.download_manager = DownloadManager(self.exe_dir)
        else:
            # Running as script - use source tree paths
            self.base_path = Path(__file__).parent.parent
            self.exe_dir = None
            self.config_file = self.base_path / "gui" / "node_settings.json"
            self.download_manager = DownloadManager(self.base_path / "gui")
        self.settings = self._load_settings()
        self.process = None
        self.output_lines = []
        self.is_running = False
        self.log_thread = None
        self._check_initial_state()
        
    def _get_default_datadir(self):
        system = platform.system()
        if system == "Darwin":
            return str(Path.home() / "Library" / "Application Support" / "Radiant")
        elif system == "Windows":
            return str(Path(os.environ.get("APPDATA", "")) / "Radiant")
        else:
            return str(Path.home() / ".radiant")
    
    def _load_settings(self):
        default = {
            "datadir": self._get_default_datadir(),
            "network": "mainnet",
            "prune": False,
            "prune_size": 550,
            "auto_start": True,
        }
        try:
            if self.config_file.exists():
                with open(self.config_file) as f:
                    default.update(json.load(f))
        except Exception:
            pass
        # Validate datadir is sane for current platform (e.g. don't use
        # macOS paths on Windows or vice versa)
        datadir = default.get("datadir", "")
        if platform.system() == "Windows":
            if datadir.startswith("/") or "/Library/" in datadir:
                default["datadir"] = self._get_default_datadir()
        elif not datadir or (datadir[1:3] == ":\\" or datadir[1:3] == ":/"):
            default["datadir"] = self._get_default_datadir()
        return default
    
    def _save_settings(self):
        try:
            self.config_file.parent.mkdir(parents=True, exist_ok=True)
            with open(self.config_file, "w") as f:
                json.dump(self.settings, f, indent=2)
        except Exception:
            pass
    
    def _check_initial_state(self):
        """Check if node is already running on GUI startup."""
        self._log("Checking for running node...")
        
        # First check if binary exists
        binary = self._find_binary("radiantd")
        
        if self._is_node_running_externally():
            self._log("Connected to existing Radiant node")
            cli = self._find_binary("radiant-cli")
            if cli:
                try:
                    result = subprocess.run([cli, "getblockchaininfo"], capture_output=True, text=True, timeout=5)
                    if result.returncode == 0:
                        info = json.loads(result.stdout)
                        blocks = info.get('blocks', 0)
                        headers = info.get('headers', 0)
                        ibd = info.get('initialblockdownload', True)
                        
                        self._log(f"Network: {info.get('chain', 'unknown')}")
                        self._log(f"Block height: {blocks:,}")
                        
                        # Show accurate sync status
                        if not ibd and blocks >= headers - 1:
                            self._log("Status: ✓ Fully synced")
                        elif headers > 0:
                            progress = (blocks / headers) * 100
                            self._log(f"Sync progress: {progress:.1f}% ({blocks:,} / {headers:,} blocks)")
                        else:
                            self._log("Status: Connecting to network...")
                except Exception:
                    pass
        elif binary:
            self._log(f"Found radiantd: {binary}")
            self._log("Ready to start node")
        else:
            self._log("Node binaries not found")
            self._log("Click 'Download Binaries' above to install")
    
    def _find_binary(self, name):
        if platform.system() == "Windows":
            name += ".exe"
        
        paths = []
        
        # Check app bundle Resources FIRST (for frozen macOS app) - highest priority
        if getattr(sys, 'frozen', False) and platform.system() == 'Darwin':
            # Running as macOS app bundle
            bundle_dir = Path(sys.executable).parent.parent  # Contents/MacOS -> Contents
            # Check Resources/binaries/radiant-core-macos-arm64/ first (correct structure)
            paths.append(bundle_dir / "Resources" / "binaries" / "radiant-core-macos-arm64" / name)
            # Fallback to other possible locations
            paths.append(bundle_dir / "Resources" / "binaries" / name)
            paths.append(bundle_dir / "Resources" / name)
        
        # Check next to running executable (for Windows, not macOS)
        if getattr(sys, 'frozen', False) and platform.system() != 'Darwin':
            exe_dir = Path(sys.executable).parent
            paths.append(exe_dir / name)
        
        # Check downloaded binaries (but only if not in app bundle)
        downloaded_path = self.download_manager.get_binary_path()
        if downloaded_path:
            paths.append(downloaded_path / name)
        
        # Common user download locations - only check AFTER app bundle
        home = Path.home()
        paths.extend([
            # User's Downloads folder
            home / "Downloads" / "radiant-core-macos-arm64" / name,
            home / "Downloads" / "radiant-core-linux-x64" / name,
            home / "Downloads" / "radiant-core-windows-x64" / name,
            # Desktop
            home / "Desktop" / "radiant-core-macos-arm64" / name,
            home / "Desktop" / "radiant-core-linux-x64" / name,
            home / "Desktop" / "radiant-core-windows-x64" / name,
        ])
        
        paths.extend([
            self.base_path / "build" / "src" / name,
            self.base_path / "src" / name,
            self.base_path / name,
            # Release binaries (v2.1.1)
            self.base_path / "releases" / "v2.1.1" / "Windows" / name,
            self.base_path / "releases" / "v2.1.0" / "Windows" / "radiant-core-windows-x64" / name,
            # Release binaries (legacy paths)
            self.base_path / "releases" / "Mac - Apple Silicon" / "radiant-core-macos-arm64" / name,
            self.base_path / "releases" / "Mac - Intel" / "radiant-core-macos-x64" / name,
            self.base_path / "releases" / "Linux" / "radiant-core-linux-x64" / name,
            self.base_path / "releases" / "Windows" / name,
            self.base_path / "releases" / "Windows" / "radiant-core-windows-x64" / name,
            Path("/usr/local/bin") / name,
            Path("/usr/bin") / name,
        ])
        for p in paths:
            if p.exists():
                return str(p)
        return None
    
    def _run_cli(self, *args, timeout=10, stdin_args=None):
        """Run a radiant-cli command and return the result.

        H13: When ``stdin_args`` is provided (a list of strings), radiant-cli is
        invoked with ``-stdin`` and those values are fed via standard input,
        one per line. radiant-cli appends stdin args *after* the argv args, so
        callers should pass only the non-sensitive command name (and any
        leading positionals) in ``args`` and put sensitive values (e.g. WIF
        private keys) in ``stdin_args``. This keeps secrets out of the process
        argv where they would otherwise be visible to ``ps``/other users.
        """
        cli = self._find_binary("radiant-cli")
        if not cli:
            return None, "CLI not found"
        cmd = [cli]
        stdin_data = None
        if stdin_args:
            cmd.append("-stdin")
        cmd.extend(args)
        if stdin_args:
            # One argument per line; radiant-cli reads until EOF and appends
            # these to the positional args after those given on argv.
            stdin_data = "\n".join(str(a) for a in stdin_args) + "\n"
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=timeout,
                input=stdin_data,
            )
            if result.returncode == 0:
                return result.stdout.strip(), None
            return None, result.stderr.strip() or f"Command failed with code {result.returncode}"
        except subprocess.TimeoutExpired:
            return None, "Command timed out"
        except Exception as e:
            return None, str(e)
    
    def _is_node_running_externally(self):
        """Check if a node is already running (started outside GUI) via RPC."""
        cli = self._find_binary("radiant-cli")
        if not cli:
            return False
        try:
            result = subprocess.run([cli, "getblockchaininfo"], capture_output=True, text=True, timeout=3)
            return result.returncode == 0
        except Exception:
            return False
    
    def get_status(self):
        # Check if node is running externally (not started by GUI)
        external_running = self._is_node_running_externally()
        actually_running = self.is_running or external_running
        
        info = {
            "running": actually_running,
            "started_by_gui": self.is_running,
            "settings": self.settings,
            "logs": self.output_lines[-100:],
            "binary_found": self._find_binary("radiantd") is not None,
        }
        
        if actually_running:
            cli = self._find_binary("radiant-cli")
            if cli:
                try:
                    result = subprocess.run(
                        [cli, "getblockchaininfo"],
                        capture_output=True, text=True, timeout=5
                    )
                    if result.returncode == 0:
                        bc_info = json.loads(result.stdout)
                        blocks = bc_info.get("blocks", 0)
                        headers = bc_info.get("headers", 0)
                        info["blocks"] = blocks
                        info["headers"] = headers
                        info["chain"] = bc_info.get("chain", "unknown")
                        info["initialblockdownload"] = bc_info.get("initialblockdownload", True)
                        
                        # Calculate accurate sync progress based on blocks vs headers
                        if headers > 0:
                            info["progress"] = (blocks / headers) * 100
                        else:
                            info["progress"] = 0
                        
                        # Mark as synced if IBD is done and blocks match headers
                        info["synced"] = (not bc_info.get("initialblockdownload", True) 
                                         and blocks >= headers - 1)
                except Exception:
                    pass
                
                try:
                    result = subprocess.run(
                        [cli, "getconnectioncount"],
                        capture_output=True, text=True, timeout=5
                    )
                    if result.returncode == 0:
                        info["peers"] = int(result.stdout.strip())
                except Exception:
                    pass
        
        return info
    
    def _log(self, msg):
        timestamp = time.strftime("%H:%M:%S")
        line = f"[{timestamp}] {msg}"
        self.output_lines.append(line)
        if len(self.output_lines) > MAX_LOG_LINES:
            self.output_lines = self.output_lines[-MAX_LOG_LINES:]
    
    def _read_output(self):
        try:
            while self.process and self.process.poll() is None:
                line = self.process.stdout.readline()
                if line:
                    self._log(line.strip())
        except Exception:
            pass
        
        if self.process:
            self._log("Node process ended")
            self.is_running = False
    
    def start(self, settings=None):
        if self.is_running:
            return {"success": False, "error": "Node already running (started by GUI)"}
        
        # Check if node is already running externally
        if self._is_node_running_externally():
            self._log("Detected existing node running - connecting to it")
            return {"success": True, "message": "Connected to existing node"}
        
        binary = self._find_binary("radiantd")
        if not binary:
            return {"success": False, "error": "radiantd binary not found. Please build the project first."}
        
        if settings:
            self.settings.update(settings)
            self._save_settings()
        
        cmd = [binary]
        
        datadir = self.settings.get("datadir")
        if datadir:
            cmd.append(f"-datadir={datadir}")
            Path(datadir).mkdir(parents=True, exist_ok=True)
        
        network = self.settings.get("network", "mainnet")
        if network == "testnet":
            cmd.append("-testnet")
        elif network == "regtest":
            cmd.append("-regtest")
        
        if self.settings.get("prune"):
            size = self.settings.get("prune_size", 550)
            cmd.append(f"-prune={size}")
        
        cmd.extend(["-daemon=0", "-printtoconsole", "-server=1", "-disablewallet=0"])
        
        self._log(f"Starting: {' '.join(cmd)}")
        
        try:
            # Set up environment with library path for bundled dylibs
            env = os.environ.copy()
            binary_dir = os.path.dirname(binary)
            libs_dir = os.path.join(binary_dir, "libs")
            if os.path.isdir(libs_dir):
                # Add bundled libs to library path for macOS
                existing_path = env.get("DYLD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = f"{libs_dir}:{existing_path}" if existing_path else libs_dir
                self._log(f"Using bundled libraries from: {libs_dir}")
            
            # On Windows, add binary directory to PATH for DLL resolution
            if platform.system() == "Windows":
                existing_path = env.get("PATH", "")
                env["PATH"] = f"{binary_dir};{existing_path}"
            
            # On Windows, use CREATE_NEW_PROCESS_GROUP to isolate the child
            # process from the parent's console signals. Without this, a
            # windowless PyInstaller exe can cause radiantd to receive
            # CTRL_CLOSE_EVENT and shut down immediately.
            popen_kwargs = dict(
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                env=env,
                cwd=binary_dir,
            )
            if platform.system() == "Windows":
                CREATE_NEW_PROCESS_GROUP = 0x00000200
                CREATE_NO_WINDOW = 0x08000000
                popen_kwargs["creationflags"] = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW
            
            self.process = subprocess.Popen(cmd, **popen_kwargs)
            self.is_running = True
            self._log("Node started successfully")
            
            self.log_thread = threading.Thread(target=self._read_output, daemon=True)
            self.log_thread.start()
            
            return {"success": True}
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def stop(self, force_external=False):
        # Check if we can stop - either GUI started it or force_external is True
        if not self.is_running and not force_external:
            # Check if there's an external node we could stop
            if self._is_node_running_externally():
                return {"success": False, "error": "Node was started externally. Use 'radiant-cli stop' in terminal to stop it."}
            return {"success": False, "error": "Node not running"}
        
        self._log("Stopping node...")
        
        cli = self._find_binary("radiant-cli")
        if cli:
            try:
                result = subprocess.run([cli, "stop"], capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    self._log("Stop command sent")
            except Exception as e:
                self._log(f"Stop command failed: {e}")
        
        if self.process:
            try:
                self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self._log("Force terminating...")
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
        
        self.process = None
        self.is_running = False
        self._log("Node stopped")
        return {"success": True}
    
    def get_info(self):
        cli = self._find_binary("radiant-cli")
        if not cli:
            return {"error": "CLI not found"}
        
        info = {}
        commands = ["getblockchaininfo", "getnetworkinfo", "getpeerinfo"]
        for cmd in commands:
            try:
                result = subprocess.run([cli, cmd], capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    info[cmd] = json.loads(result.stdout)
            except Exception as e:
                info[cmd] = {"error": str(e)}
        return info
    
    def _is_node_available(self):
        """Check if node is available (started by GUI or running externally)."""
        return self.is_running or self._is_node_running_externally()
    
    def _is_wallet_supported(self):
        """Check if node was compiled with wallet support."""
        result, err = self._run_cli("help", "getbalance")
        if err and "Method not found" in err:
            return False
        return True
    
    # Wallet functions
    def get_wallet_info(self):
        if not self._is_node_available():
            return {"error": "Node not running"}
        
        # Check if wallet is supported
        if not self._is_wallet_supported():
            return {
                "error": "Wallet not available",
                "wallet_disabled": True,
                "message": "Node was compiled without wallet support. Rebuild with -DBUILD_RADIANT_WALLET=ON"
            }
        
        result = {}
        
        # Get balance
        balance, err = self._run_cli("getbalance")
        if balance is not None:
            try:
                result["balance"] = float(balance)
            except:
                result["balance"] = 0
        else:
            result["balance"] = 0
            result["balance_error"] = err
        
        # Get unconfirmed balance
        unconf, _ = self._run_cli("getunconfirmedbalance")
        if unconf is not None:
            try:
                result["unconfirmed"] = float(unconf)
            except:
                result["unconfirmed"] = 0
        else:
            result["unconfirmed"] = 0
        
        # Get wallet info
        winfo, _ = self._run_cli("getwalletinfo")
        if winfo:
            try:
                result["wallet_info"] = json.loads(winfo)
            except:
                pass
        
        return result
    
    def get_new_address(self, label=""):
        if not self._is_node_available():
            return {"error": "Node not running"}

        # New-4: validate label before passing to subprocess
        ok, verr = _validate_label(label)
        if not ok:
            return {"success": False, "error": verr}

        args = ["getnewaddress"]
        if label:
            args.append(label)

        address, err = self._run_cli(*args)
        if address:
            return {"success": True, "address": address}
        return {"success": False, "error": err or "Failed to generate address"}
    
    def get_addresses(self):
        if not self._is_node_available():
            return {"error": "Node not running"}
        
        # Get receiving addresses
        result, err = self._run_cli("listreceivedbyaddress", "0", "true")
        if result:
            try:
                addresses = json.loads(result)
                return {"success": True, "addresses": addresses}
            except:
                pass
        
        return {"success": False, "error": err or "Failed to list addresses", "addresses": []}
    
    def send_rxd(self, address, amount, subtract_fee=False):
        if not self._is_node_available():
            return {"error": "Node not running"}
        
        if not address:
            return {"success": False, "error": "Address is required"}
        
        try:
            amount = float(amount)
            if amount <= 0:
                return {"success": False, "error": "Amount must be positive"}
        except:
            return {"success": False, "error": "Invalid amount"}
        
        # Validate address
        valid, _ = self._run_cli("validateaddress", address)
        if valid:
            try:
                vdata = json.loads(valid)
                if not vdata.get("isvalid"):
                    return {"success": False, "error": "Invalid RXD address"}
            except:
                pass
        
        # Send transaction
        # If subtract_fee is True, the fee will be deducted from the amount (for send-max)
        if subtract_fee:
            # sendtoaddress "address" amount "comment" "comment_to" subtractfeefromamount
            txid, err = self._run_cli("sendtoaddress", address, str(amount), "", "", "true")
        else:
            txid, err = self._run_cli("sendtoaddress", address, str(amount))
        
        if txid:
            self._log(f"Sent {amount} RXD to {address[:16]}... - TXID: {txid[:16]}...")
            return {"success": True, "txid": txid}
        
        return {"success": False, "error": err or "Transaction failed"}
    
    def get_max_sendable_amount(self, address=None):
        """Calculate maximum sendable amount by deducting estimated transaction fee from balance."""
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}
        
        # Get current balance
        balance, err = self._run_cli("getbalance")
        if balance is None:
            return {"success": False, "error": err or "Failed to get balance"}
        
        try:
            balance = float(balance)
        except:
            return {"success": False, "error": "Invalid balance"}
        
        if balance <= 0:
            return {"success": True, "max_amount": 0, "fee": 0, "balance": 0}
        
        # Count actual UTXOs to estimate transaction size accurately
        # Use minconf=0 to include unconfirmed UTXOs
        utxo_count = 1  # Default minimum
        utxo_result, _ = self._run_cli("listunspent", "0")
        if utxo_result:
            try:
                utxos = json.loads(utxo_result)
                utxo_count = len(utxos) if utxos else 1
            except:
                pass
        
        # Get relay fee from network info (minimum fee rate the network will accept)
        # Radiant default: 10,000,000 satoshis/kB = 0.1 RXD/kB (from validation.h)
        relay_fee_per_kb = 0.1  # Default: 0.1 RXD/kB (10 sat/byte) - Radiant's minimum
        net_result, _ = self._run_cli("getnetworkinfo")
        if net_result:
            try:
                net_info = json.loads(net_result)
                if "relayfee" in net_info and net_info["relayfee"] > 0:
                    relay_fee_per_kb = net_info["relayfee"]
            except:
                pass
        
        # Get fee rate using estimatesmartfee (returns fee rate in RXD/kB)
        fee_rate_per_kb = relay_fee_per_kb  # Start with relay fee as minimum
        
        fee_result, _ = self._run_cli("estimatesmartfee", "6")
        if fee_result:
            try:
                fee_data = json.loads(fee_result)
                if "feerate" in fee_data and fee_data["feerate"] > 0:
                    # Use the higher of estimatesmartfee or relay fee
                    fee_rate_per_kb = max(fee_data["feerate"], relay_fee_per_kb)
            except:
                pass
        
        # Calculate transaction size based on actual UTXO count
        # P2PKH transaction size formula:
        # - Base overhead: ~10 bytes
        # - Per input (UTXO): ~148 bytes each
        # - Per output: ~34 bytes each (we have 1 output for send-all)
        tx_overhead = 10
        bytes_per_input = 148
        bytes_per_output = 34
        num_outputs = 1  # Sending all to one address
        
        estimated_tx_bytes = tx_overhead + (bytes_per_input * utxo_count) + (bytes_per_output * num_outputs)
        estimated_tx_kb = estimated_tx_bytes / 1000.0
        
        # Calculate fee
        estimated_fee = fee_rate_per_kb * estimated_tx_kb
        
        # Add 25% safety margin to avoid "insufficient fee" errors
        estimated_fee = estimated_fee * 1.25
        
        # Round up to 8 decimal places
        estimated_fee = round(estimated_fee + 0.000000005, 8)
        
        max_amount = balance - estimated_fee
        if max_amount < 0:
            max_amount = 0
        
        # Round down to 8 decimal places to avoid floating point issues
        max_amount = float(f"{max_amount:.8f}")
        
        return {
            "success": True,
            "max_amount": max_amount,
            "fee": estimated_fee,
            "balance": balance,
            "utxo_count": utxo_count,
            "tx_size_bytes": estimated_tx_bytes,
            "fee_rate_per_kb": fee_rate_per_kb,
            "relay_fee_per_kb": relay_fee_per_kb
        }
    
    def get_transactions(self, count=20):
        if not self._is_node_available():
            return {"error": "Node not running"}
        
        result, err = self._run_cli("listtransactions", "*", str(count))
        if result:
            try:
                txs = json.loads(result)
                return {"success": True, "transactions": txs}
            except:
                pass
        
        return {"success": False, "error": err or "Failed to list transactions", "transactions": []}
    
    # Wallet backup/restore functions
    def dump_privkey(self, address):
        """Export private key for a specific address."""
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}

        if not address:
            return {"success": False, "error": "Address is required"}

        # New-4: validate the address before passing to subprocess
        ok, verr = _validate_address(address)
        if not ok:
            return {"success": False, "error": verr}

        # H13: the *input* (address) is not secret; the returned WIF is. Feed
        # the address via stdin anyway so it stays out of argv for consistency
        # with importprivkey.
        privkey, err = self._run_cli("dumpprivkey", stdin_args=[address])
        if privkey:
            self._log(f"Exported private key for {address[:16]}...")
            return {"success": True, "privkey": privkey, "address": address}
        
        return {"success": False, "error": err or "Failed to export private key"}
    
    def import_privkey(self, privkey, label="", rescan=True):
        """Import a private key into the wallet."""
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}

        if not privkey:
            return {"success": False, "error": "Private key is required"}

        # New-4: validate inputs before passing to subprocess
        ok, verr = _validate_privkey(privkey)
        if not ok:
            return {"success": False, "error": verr}
        ok, verr = _validate_label(label)
        if not ok:
            return {"success": False, "error": verr}

        self._log(f"Importing private key (rescan={rescan})...")

        # H13: keep the WIF out of argv. radiant-cli -stdin appends stdin args
        # after argv args, so ALL positionals (privkey, [label], rescan) are
        # fed via stdin in order; argv carries only the command name. The label
        # is omitted when empty so 'rescan' keeps its positional slot (matching
        # the original argv-based behavior).
        stdin_args = [privkey]
        if label:
            stdin_args.append(label)
        stdin_args.append(str(rescan).lower())
        result, err = self._run_cli(
            "importprivkey", timeout=300, stdin_args=stdin_args
        )  # Rescan can take a while
        if err:
            return {"success": False, "error": err}
        
        self._log("Private key imported successfully")
        return {"success": True, "message": "Private key imported successfully"}
    
    def backup_wallet(self, destination=None):
        """Backup wallet to a file."""
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}
        
        backup_dir = Path.home() / "Documents" / "RadiantBackups"
        if not destination:
            # Default backup location
            backup_dir.mkdir(parents=True, exist_ok=True)
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            destination = str(backup_dir / f"wallet_backup_{timestamp}.dat")
        else:
            # New-4: validate caller-supplied destination (reject flag smuggling,
            # NUL bytes, and confine to the RadiantBackups directory).
            ok, verr = _validate_backup_destination(destination)
            if not ok:
                return {"success": False, "error": verr}
            backup_dir.mkdir(parents=True, exist_ok=True)

        result, err = self._run_cli("backupwallet", destination)
        if err:
            return {"success": False, "error": err}
        
        self._log(f"Wallet backed up to: {destination}")
        return {"success": True, "path": destination, "message": f"Wallet backed up to {destination}"}
    
    def get_all_addresses_with_keys(self):
        """Get all addresses in the wallet for key export."""
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}
        
        # Get all addresses using listaddressgroupings
        result, err = self._run_cli("listaddressgroupings")
        addresses = []
        if result:
            try:
                groups = json.loads(result)
                for group in groups:
                    for addr_info in group:
                        if len(addr_info) >= 1:
                            addresses.append({
                                "address": addr_info[0],
                                "balance": addr_info[1] if len(addr_info) > 1 else 0,
                                "label": addr_info[2] if len(addr_info) > 2 else ""
                            })
            except:
                pass
        
        # Also get addresses from listreceivedbyaddress
        result2, _ = self._run_cli("listreceivedbyaddress", "0", "true")
        if result2:
            try:
                received = json.loads(result2)
                existing = {a["address"] for a in addresses}
                for r in received:
                    if r.get("address") and r["address"] not in existing:
                        addresses.append({
                            "address": r["address"],
                            "balance": r.get("amount", 0),
                            "label": r.get("label", "")
                        })
            except:
                pass
        
        return {"success": True, "addresses": addresses}
    
    # Seed phrase (BIP39 mnemonic) functions
    def generate_seed_phrase(self, words=12):
        """Generate a new BIP39 seed phrase."""
        if not BIP39_AVAILABLE:
            return {"success": False, "error": "BIP39 module not available"}
        
        strength = 128 if words == 12 else 256
        try:
            mnemonic = generate_mnemonic(strength)
            return {"success": True, "mnemonic": mnemonic, "words": words}
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def import_seed_phrase(self, mnemonic, passphrase="", rescan=True):
        """Import wallet from BIP39 seed phrase."""
        if not BIP39_AVAILABLE:
            return {"success": False, "error": "BIP39 module not available"}
        
        if not self._is_node_available():
            return {"success": False, "error": "Node not running"}
        
        if not mnemonic:
            return {"success": False, "error": "Seed phrase is required"}
        
        # Validate mnemonic
        if not validate_mnemonic(mnemonic):
            return {"success": False, "error": "Invalid seed phrase. Must be 12, 15, 18, 21, or 24 words from BIP39 wordlist."}
        
        try:
            # Convert mnemonic to WIF
            wif = mnemonic_to_wif(mnemonic, passphrase)
            
            self._log(f"Importing seed phrase (rescan={rescan})...")

            # H13: feed the derived WIF via stdin so it never appears in argv.
            stdin_args = [wif, "seed-phrase-import", str(rescan).lower()]
            result, err = self._run_cli(
                "importprivkey", timeout=300, stdin_args=stdin_args
            )
            if err:
                return {"success": False, "error": err}
            
            self._log("Seed phrase imported successfully")
            return {"success": True, "message": "Seed phrase imported successfully"}
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    # System install function
    def install_to_system(self):
        """Install binaries to user's local bin directory."""
        binary_path = self.download_manager.get_binary_path()
        if not binary_path or not binary_path.exists():
            return {"success": False, "error": "Binaries not found. Download them first."}
        
        # Use ~/bin instead of /usr/local/bin to avoid sudo
        user_bin = Path.home() / "bin"
        user_bin.mkdir(parents=True, exist_ok=True)
        
        binaries = ["radiantd", "radiant-cli", "radiant-tx"]
        installed = []
        
        for binary in binaries:
            src = binary_path / binary
            dst = user_bin / binary
            if src.exists():
                try:
                    import shutil
                    shutil.copy2(src, dst)
                    dst.chmod(0o755)
                    installed.append(binary)
                except Exception as e:
                    return {"success": False, "error": f"Failed to copy {binary}: {e}"}
        
        if installed:
            self._log(f"Installed to ~/bin: {', '.join(installed)}")
            # Provide shell config instructions
            shell_config = "~/.zshrc" if os.path.exists(os.path.expanduser("~/.zshrc")) else "~/.bashrc"
            return {
                "success": True,
                "installed": installed,
                "path": str(user_bin),
                "message": f"Installed {len(installed)} binaries to ~/bin",
                "instructions": f'Add to PATH by running: echo \'export PATH="$HOME/bin:$PATH"\' >> {shell_config} && source {shell_config}'
            }
        
        return {"success": False, "error": "No binaries found to install"}


# Global node manager
manager = NodeManager()

# C3 Security: Generate per-launch random CSRF token
# This token is embedded in the HTML and required on all API calls
import secrets
_SECURITY_TOKEN = secrets.token_hex(32)

def _get_html_with_token():
    """Return HTML page with embedded security token."""
    # Inject token into HTML for JavaScript to use
    token_script = f'''<script>
    // Security token for API authentication (per-launch random)
    window._RADIANT_SECURITY_TOKEN = "{_SECURITY_TOKEN}";
</script>'''
    # Insert before closing </head> tag
    return HTML_PAGE.replace('</head>', token_script + '\n</head>')

HTML_PAGE = '''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Radiant Core Node</title>
    <style>
        :root {
            --bg-primary: #0f0f14;
            --bg-secondary: #1a1a24;
            --bg-tertiary: #252532;
            --text-primary: #f0f0f5;
            --text-secondary: #a0a0b0;
            --text-muted: #606070;
            --border-color: #2a2a3a;
            --accent: #00d9ff;
            --accent-green: #00e88a;
            --accent-red: #ff5a6a;
            --accent-orange: #ffaa00;
        }
        [data-theme="light"] {
            --bg-primary: #f5f7fa;
            --bg-secondary: #ffffff;
            --bg-tertiary: #e8edf2;
            --text-primary: #1a1a2e;
            --text-secondary: #4a4a5a;
            --text-muted: #8a8a9a;
            --border-color: #d0d5dd;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg-primary);
            min-height: 100vh;
            color: var(--text-primary);
            padding: 20px;
            transition: background 0.3s, color 0.3s;
        }
        .container { max-width: 900px; margin: 0 auto; }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 20px 0;
            border-bottom: 1px solid var(--border-color);
            margin-bottom: 20px;
        }
        .header-left { display: flex; align-items: center; gap: 12px; }
        .logo { width: 40px; height: 40px; }
        .logo-dark { display: block; }
        .logo-light { display: none; }
        [data-theme="light"] .logo-dark { display: none; }
        [data-theme="light"] .logo-light { display: block; }
        h1 {
            font-size: 24px;
            color: var(--text-primary);
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .header-right { display: flex; align-items: center; gap: 12px; }
        .theme-toggle {
            background: var(--bg-tertiary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 8px 12px;
            cursor: pointer;
            color: var(--text-secondary);
            font-size: 16px;
        }
        .theme-toggle:hover { background: var(--border-color); }
        .status {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 13px;
            padding: 8px 14px;
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 20px;
        }
        .status-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: var(--text-muted);
            transition: background 0.3s;
        }
        .status-dot.running { background: var(--accent-green); box-shadow: 0 0 8px var(--accent-green); }
        .status-dot.starting { background: var(--accent-orange); animation: pulse 1s infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }
        
        .tabs {
            display: flex;
            gap: 4px;
            margin-bottom: 20px;
            background: var(--bg-secondary);
            padding: 4px;
            border-radius: 10px;
            border: 1px solid var(--border-color);
        }
        .tab {
            flex: 1;
            padding: 10px 24px;
            background: transparent;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            border-radius: 8px;
            transition: all 0.2s;
        }
        .tab:hover { color: var(--text-primary); background: var(--bg-tertiary); }
        .tab.active {
            color: #fff;
            background: var(--accent);
        }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        
        .controls {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            flex-wrap: wrap;
        }
        button {
            padding: 12px 24px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: all 0.2s;
        }
        .btn-start {
            background: linear-gradient(135deg, var(--accent), var(--accent-green));
            color: #0a0a0f;
        }
        .btn-start:hover { transform: translateY(-2px); box-shadow: 0 4px 15px rgba(0,217,255,0.4); }
        .btn-start:disabled { background: var(--bg-tertiary); color: var(--text-muted); cursor: not-allowed; transform: none; box-shadow: none; }
        .btn-stop { background: var(--accent-red); color: white; }
        .btn-stop:hover { opacity: 0.9; }
        .btn-stop:disabled { background: var(--bg-tertiary); color: var(--text-muted); cursor: not-allowed; }
        .btn-secondary { background: var(--bg-tertiary); color: var(--text-primary); border: 1px solid var(--border-color); }
        .btn-secondary:hover { background: var(--border-color); }
        .btn-send { background: linear-gradient(135deg, #ff6b6b, #ff8e53); color: white; }
        .btn-send:hover { transform: translateY(-2px); box-shadow: 0 4px 15px rgba(255,107,107,0.4); }
        .btn-receive { background: linear-gradient(135deg, var(--accent), var(--accent-green)); color: #0a0a0f; }
        .btn-small { padding: 8px 16px; font-size: 12px; }
        
        .panel {
            background: var(--bg-secondary);
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            border: 1px solid var(--border-color);
        }
        .panel h2 {
            font-size: 13px;
            margin-bottom: 15px;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }
        .stat {
            text-align: center;
            padding: 15px;
            background: var(--bg-tertiary);
            border-radius: 8px;
        }
        .stat-value {
            font-size: 24px;
            font-weight: bold;
            color: var(--accent);
        }
        .stat-value.balance { color: var(--accent-green); }
        .stat-label { font-size: 12px; color: var(--text-muted); margin-top: 5px; }
        
        .settings-grid {
            display: grid;
            gap: 15px;
        }
        .setting {
            display: flex;
            align-items: center;
            gap: 15px;
            flex-wrap: wrap;
        }
        .setting label { min-width: 100px; font-size: 14px; color: var(--text-secondary); }
        .setting input, .setting select {
            flex: 1;
            min-width: 150px;
            padding: 10px;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            background: var(--bg-primary);
            color: var(--text-primary);
            font-size: 14px;
        }
        .setting input:focus, .setting select:focus {
            outline: none;
            border-color: var(--accent);
        }
        .setting input[type="checkbox"] {
            width: 20px;
            height: 20px;
            min-width: 20px;
            flex: none;
            accent-color: var(--accent);
        }
        .prune-size { width: 100px !important; min-width: 100px !important; flex: none !important; }
        
        .log-container {
            background: var(--bg-primary);
            border-radius: 8px;
            padding: 15px;
            height: 200px;
            overflow-y: auto;
            font-family: "Monaco", "Menlo", monospace;
            font-size: 12px;
            line-height: 1.6;
            border: 1px solid var(--border-color);
        }
        .log-line { color: var(--text-muted); }
        .log-line:last-child { color: var(--accent-green); }
        
        /* Wallet Styles */
        .wallet-balance {
            text-align: center;
            padding: 30px;
            background: linear-gradient(135deg, rgba(0,217,255,0.08), rgba(0,232,138,0.08));
            border-radius: 12px;
            margin-bottom: 20px;
            border: 1px solid var(--border-color);
        }
        .wallet-balance .amount {
            font-size: 42px;
            font-weight: bold;
            color: var(--accent-green);
        }
        .wallet-balance .currency { font-size: 20px; color: var(--text-muted); margin-left: 8px; }
        .wallet-balance .unconfirmed { font-size: 14px; color: var(--accent-orange); margin-top: 5px; }
        
        .wallet-actions {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin-bottom: 20px;
        }
        .wallet-action {
            padding: 20px;
            background: var(--bg-secondary);
            border-radius: 12px;
            text-align: center;
            border: 1px solid var(--border-color);
        }
        .wallet-action h3 { margin-bottom: 15px; color: var(--text-primary); }
        
        .address-box {
            background: var(--bg-primary);
            padding: 15px;
            border-radius: 8px;
            font-family: monospace;
            font-size: 13px;
            word-break: break-all;
            margin: 10px 0;
            color: var(--accent);
            cursor: pointer;
            transition: all 0.2s;
            border: 1px solid var(--border-color);
        }
        .address-box:hover { border-color: var(--accent); }
        .address-box.copied { background: rgba(0,232,138,0.15); border-color: var(--accent-green); }
        
        .form-group {
            margin-bottom: 15px;
            text-align: left;
        }
        .form-group label {
            display: block;
            margin-bottom: 5px;
            color: var(--text-secondary);
            font-size: 13px;
        }
        .form-group input {
            width: 100%;
            padding: 12px;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            background: var(--bg-primary);
            color: var(--text-primary);
            font-size: 14px;
        }
        .form-group input:focus { outline: none; border-color: var(--accent); }
        
        .tx-list {
            max-height: 300px;
            overflow-y: auto;
        }
        .tx-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px;
            border-bottom: 1px solid var(--border-color);
        }
        .tx-item:last-child { border-bottom: none; }
        .tx-amount { font-weight: bold; }
        .tx-amount.positive { color: var(--accent-green); }
        .tx-amount.negative { color: var(--accent-red); }
        .tx-info { font-size: 12px; color: var(--text-muted); }
        .tx-confirmations { font-size: 11px; padding: 2px 6px; background: var(--bg-tertiary); border-radius: 4px; color: var(--text-secondary); }
        
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.7);
            justify-content: center;
            align-items: center;
            z-index: 1000;
            backdrop-filter: blur(4px);
        }
        .modal.show { display: flex; }
        .modal-content {
            background: var(--bg-secondary);
            border-radius: 12px;
            padding: 25px;
            max-width: 700px;
            max-height: 80vh;
            overflow-y: auto;
            width: 90%;
            border: 1px solid var(--border-color);
        }
        .modal-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        .modal-close {
            background: var(--bg-tertiary);
            border: none;
            color: var(--text-secondary);
            font-size: 20px;
            cursor: pointer;
            width: 32px;
            height: 32px;
            border-radius: 6px;
        }
        .modal-close:hover { background: var(--border-color); }
        pre {
            background: var(--bg-primary);
            padding: 15px;
            border-radius: 8px;
            overflow-x: auto;
            font-size: 11px;
            line-height: 1.5;
            border: 1px solid var(--border-color);
        }
        
        .alert {
            padding: 12px 15px;
            border-radius: 8px;
            margin-bottom: 15px;
            font-size: 14px;
        }
        .alert-success { background: rgba(0,232,138,0.2); color: var(--accent-green); }
        .alert-error { background: rgba(255,90,106,0.2); color: var(--accent-red); }
        .alert-warning { background: rgba(255,170,0,0.2); color: var(--accent-orange); }
        
        footer {
            text-align: center;
            padding: 20px;
            color: var(--text-muted);
            font-size: 12px;
        }
        footer a { color: var(--accent); text-decoration: none; }
        footer a:hover { text-decoration: underline; }
        
        .disabled-overlay {
            position: relative;
        }
        .disabled-overlay::after {
            content: "Start node to use wallet";
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: var(--bg-primary);
            opacity: 0.92;
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--text-muted);
            font-size: 16px;
            border-radius: 12px;
        }
        
        .download-panel {
            background: linear-gradient(135deg, rgba(0,217,255,0.1), rgba(0,232,138,0.1));
            border: 2px dashed var(--accent);
            border-radius: 12px;
            padding: 30px;
            text-align: center;
            margin-bottom: 20px;
        }
        .download-panel h2 {
            color: var(--text-primary);
            margin-bottom: 10px;
            font-size: 20px;
        }
        .download-panel p {
            color: var(--text-secondary);
            margin-bottom: 20px;
        }
        .download-panel .platform-detected {
            background: var(--bg-secondary);
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            border: 1px solid var(--border-color);
        }
        .download-panel .platform-name {
            font-size: 16px;
            font-weight: 600;
            color: var(--accent);
        }
        .btn-download {
            background: linear-gradient(135deg, var(--accent), var(--accent-green));
            color: #0a0a0f;
            padding: 15px 30px;
            font-size: 16px;
            font-weight: 700;
        }
        .btn-download:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(0,217,255,0.5);
        }
        .btn-download:disabled {
            background: var(--bg-tertiary);
            color: var(--text-muted);
            cursor: not-allowed;
            transform: none;
            box-shadow: none;
        }
        .progress-bar {
            width: 100%;
            height: 8px;
            background: var(--bg-tertiary);
            border-radius: 4px;
            overflow: hidden;
            margin: 15px 0;
        }
        .progress-bar-fill {
            height: 100%;
            background: linear-gradient(90deg, var(--accent), var(--accent-green));
            border-radius: 4px;
            transition: width 0.3s ease;
        }
        .download-status {
            font-size: 14px;
            color: var(--text-secondary);
            margin-top: 10px;
        }
        .download-status.error { color: var(--accent-red); }
        .download-status.complete { color: var(--accent-green); }
        .manual-download {
            margin-top: 20px;
            padding-top: 20px;
            border-top: 1px solid var(--border-color);
        }
        .manual-download a {
            color: var(--accent);
            text-decoration: none;
        }
        .manual-download a:hover { text-decoration: underline; }
    </style>
</head>
<body data-theme="dark">
    <div class="container">
        <header>
            <div class="header-left">
                <img class="logo logo-dark" src="/logo-light.svg" alt="Radiant Logo">
                <img class="logo logo-light" src="/logo-dark.svg" alt="Radiant Logo">
                <h1>Radiant Core Node</h1>
            </div>
            <div class="header-right">
                <button class="theme-toggle" onclick="toggleTheme()" title="Toggle theme">🌙</button>
                <div class="status">
                    <div class="status-dot" id="statusDot"></div>
                    <span id="statusText">Checking...</span>
                </div>
            </div>
        </header>
        
        <div class="tabs">
            <button class="tab active" onclick="switchTab('node')">Node</button>
            <button class="tab" onclick="switchTab('wallet')">Wallet</button>
        </div>
        
        <!-- NODE TAB -->
        <div id="nodeTab" class="tab-content active">
            <!-- Download Panel (shown when binaries not found) -->
            <div id="downloadPanel" class="download-panel" style="display:none;">
                <h2>📦 Download Radiant Core</h2>
                <p>Node binaries not found. Download the pre-built binaries for your platform.</p>
                <div class="platform-detected">
                    <div style="font-size:12px;color:var(--text-muted);margin-bottom:5px;">Detected Platform</div>
                    <div class="platform-name" id="platformName">Detecting...</div>
                </div>
                <button class="btn-download" id="downloadBtn" onclick="startDownload()">
                    ⬇ Download Binaries
                </button>
                <div class="progress-bar" id="downloadProgress" style="display:none;">
                    <div class="progress-bar-fill" id="progressFill" style="width:0%"></div>
                </div>
                <div class="download-status" id="downloadStatus"></div>
                <div class="manual-download">
                    <small>Or download manually from <a href="https://github.com/Radiant-Core/Radiant-Core/releases/tag/v3.0.0" target="_blank">GitHub Releases</a></small>
                </div>
            </div>
            
            <div class="controls">
                <button class="btn-start" id="startBtn" onclick="startNode()">▶ Start Node</button>
                <button class="btn-stop" id="stopBtn" onclick="stopNode()" disabled>■ Stop Node</button>
                <button class="btn-secondary" onclick="showInfo()">ℹ Node Info</button>
            </div>
            
            <div class="panel" id="statsPanel" style="display:none;">
                <h2>Node Status</h2>
                <div class="stats">
                    <div class="stat">
                        <div class="stat-value" id="blockHeight">-</div>
                        <div class="stat-label">Block Height</div>
                    </div>
                    <div class="stat">
                        <div class="stat-value" id="syncProgress">-</div>
                        <div class="stat-label">Sync Progress</div>
                    </div>
                    <div class="stat">
                        <div class="stat-value" id="peerCount">-</div>
                        <div class="stat-label">Peers</div>
                    </div>
                    <div class="stat">
                        <div class="stat-value" id="networkName">-</div>
                        <div class="stat-label">Network</div>
                    </div>
                </div>
            </div>
            
            <div class="panel">
                <h2>Settings</h2>
                <div class="settings-grid">
                    <div class="setting">
                        <label>Network</label>
                        <select id="network">
                            <option value="mainnet">Mainnet</option>
                            <option value="testnet">Testnet</option>
                            <option value="regtest">Regtest</option>
                        </select>
                    </div>
                    <div class="setting">
                        <label>Data Dir</label>
                        <input type="text" id="datadir" placeholder="Blockchain data directory">
                    </div>
                    <div class="setting">
                        <label>Auto-start</label>
                        <input type="checkbox" id="autoStart" checked>
                        <span style="color:#888;">Start node when app opens</span>
                    </div>
                    <div class="setting">
                        <label>Pruning</label>
                        <input type="checkbox" id="prune" onchange="togglePrune()">
                        <span style="color:#888;">Enable (saves disk space)</span>
                        <input type="number" id="pruneSize" class="prune-size" value="550" min="550" disabled>
                        <span style="color:#888;">MB</span>
                    </div>
                </div>
            </div>
            
            <div class="panel">
                <h2>Log Output</h2>
                <div class="log-container" id="logOutput">
                    <div class="log-line">Initializing...</div>
                </div>
            </div>
        </div>
        
        <!-- WALLET TAB -->
        <div id="walletTab" class="tab-content">
            <div id="walletContent">
                <div class="wallet-balance">
                    <div><span class="amount" id="walletBalance">0.00</span><span class="currency">RXD</span></div>
                    <div class="unconfirmed" id="unconfirmedBalance"></div>
                </div>
                
                <div class="wallet-actions">
                    <div class="wallet-action">
                        <h3>Receive RXD</h3>
                        <p style="font-size:13px;color:#888;margin-bottom:15px;">Share this address to receive RXD</p>
                        <div class="address-box" id="receiveAddress" onclick="copyAddress()" title="Click to copy">
                            Generate a new address
                        </div>
                        <button class="btn-receive btn-small" onclick="generateAddress()">New Address</button>
                    </div>
                    <div class="wallet-action">
                        <h3>Send RXD</h3>
                        <div class="form-group">
                            <label>Recipient Address</label>
                            <input type="text" id="sendAddress" placeholder="Enter RXD address">
                        </div>
                        <div class="form-group">
                            <label>Amount (RXD)</label>
                            <div style="display:flex;gap:8px;">
                                <input type="number" id="sendAmount" placeholder="0.00" step="0.00000001" min="0" style="flex:1;">
                                <button class="btn-secondary btn-small" onclick="setMaxAmount()" style="white-space:nowrap;" title="Send all available balance minus fee">Max</button>
                            </div>
                            <div id="feeEstimate" style="font-size:11px;color:var(--text-muted);margin-top:5px;"></div>
                        </div>
                        <button class="btn-send" onclick="sendRXD()">Send RXD</button>
                    </div>
                </div>
                
                <div class="panel">
                    <h2>Recent Transactions</h2>
                    <div class="tx-list" id="txList">
                        <div style="text-align:center;color:#888;padding:20px;">Loading transactions...</div>
                    </div>
                </div>
                
                <div class="panel">
                    <h2>🔐 Backup & Restore</h2>
                    
                    <!-- Seed Phrase Section -->
                    <div style="background:linear-gradient(135deg,rgba(0,217,255,0.1),rgba(0,232,138,0.1));border-radius:8px;padding:15px;margin-bottom:15px;">
                        <h4 style="margin-bottom:10px;color:var(--accent);">🌱 Seed Phrase (Recommended)</h4>
                        <p style="font-size:12px;color:var(--text-muted);margin-bottom:10px;">Recovery phrase (12-24 words) - write it down and store safely!</p>
                        <div style="display:flex;gap:10px;margin-bottom:10px;">
                            <button class="btn-secondary btn-small" onclick="generateSeedPhrase()">Generate New Seed</button>
                            <button class="btn-secondary btn-small" onclick="showImportSeedModal()">Import Seed Phrase</button>
                        </div>
                        <div id="seedPhraseDisplay" style="display:none;background:var(--bg-tertiary);padding:12px;border-radius:6px;margin-top:10px;">
                            <div style="font-size:11px;color:var(--accent-orange);margin-bottom:8px;">⚠️ Write this down! Never share or store digitally!</div>
                            <div id="seedWords" style="font-family:monospace;font-size:14px;line-height:1.8;color:var(--text-primary);word-spacing:8px;"></div>
                            <button class="btn-small" style="margin-top:10px;font-size:11px;" onclick="copySeedPhrase()">Copy to Clipboard</button>
                        </div>
                    </div>
                    
                    <div style="display:grid;grid-template-columns:1fr 1fr;gap:15px;">
                        <div>
                            <h4 style="margin-bottom:10px;color:var(--text-primary);">Backup Wallet</h4>
                            <p style="font-size:12px;color:var(--text-muted);margin-bottom:10px;">Create a backup of your wallet file</p>
                            <button class="btn-secondary btn-small" onclick="backupWallet()">📁 Backup Wallet File</button>
                        </div>
                        <div>
                            <h4 style="margin-bottom:10px;color:var(--text-primary);">Export Private Key</h4>
                            <p style="font-size:12px;color:var(--text-muted);margin-bottom:10px;">Export key for a specific address</p>
                            <button class="btn-secondary btn-small" onclick="showExportKeyModal()">🔑 Export Key</button>
                        </div>
                    </div>
                    <div style="margin-top:15px;padding-top:15px;border-top:1px solid var(--border-color);">
                        <h4 style="margin-bottom:10px;color:var(--text-primary);">Import Private Key</h4>
                        <div class="form-group" style="margin-bottom:10px;">
                            <input type="text" id="importPrivkey" placeholder="Enter private key (WIF format)" style="font-family:monospace;">
                        </div>
                        <div style="display:flex;gap:10px;align-items:center;">
                            <button class="btn-secondary btn-small" onclick="importPrivkey()">📥 Import Key</button>
                            <label style="font-size:12px;color:var(--text-muted);">
                                <input type="checkbox" id="rescanChain" checked> Rescan blockchain (slow but finds old transactions)
                            </label>
                        </div>
                    </div>
                    <div id="backupStatus" class="download-status" style="margin-top:10px;"></div>
                </div>
            </div>
        </div>
        
        <footer>
            Need help? Visit <a href="https://radiantblockchain.org" target="_blank">radiantblockchain.org</a>
        </footer>
    </div>
    
    <div class="modal" id="infoModal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Node Information</h2>
                <button class="modal-close" onclick="closeModal()">&times;</button>
            </div>
            <pre id="infoContent">Loading...</pre>
        </div>
    </div>
    
    <div class="modal" id="exportKeyModal">
        <div class="modal-content" style="max-width:500px;">
            <div class="modal-header">
                <h2>🔑 Export Private Key</h2>
                <button class="modal-close" onclick="closeExportModal()">&times;</button>
            </div>
            <div style="margin-bottom:15px;">
                <p style="color:var(--text-secondary);font-size:13px;margin-bottom:15px;">
                    ⚠️ <strong>Warning:</strong> Never share your private key. Anyone with this key can access your funds.
                </p>
                <div class="form-group">
                    <label>Address to export</label>
                    <input type="text" id="exportAddress" placeholder="Enter your RXD address">
                </div>
                <button class="btn-secondary" onclick="exportPrivkey()" style="width:100%;">Export Private Key</button>
            </div>
            <div id="exportedKey" style="display:none;">
                <label style="color:var(--text-muted);font-size:12px;">Private Key (WIF format) - Click to copy:</label>
                <div class="address-box" id="privkeyDisplay" onclick="copyPrivkey()" style="background:var(--bg-tertiary);color:var(--accent-orange);word-break:break-all;"></div>
            </div>
        </div>
    </div>
    
    <div class="modal" id="importSeedModal">
        <div class="modal-content" style="max-width:650px;">
            <div class="modal-header">
                <h2>🌱 Import Seed Phrase</h2>
                <button class="modal-close" onclick="closeImportSeedModal()">&times;</button>
            </div>
            <div>
                <p style="color:var(--text-secondary);font-size:13px;margin-bottom:15px;">
                    Enter your seed phrase (12, 15, 18, 21, or 24 words) to restore your wallet.
                </p>
                <div class="form-group">
                    <label>Seed Phrase</label>
                    <textarea id="importSeedWords" rows="4" placeholder="Enter your seed words separated by spaces (12, 15, 18, 21, or 24 words)" style="font-family:monospace;resize:vertical;min-height:100px;line-height:1.6;"></textarea>
                </div>
                <div class="form-group">
                    <label>Passphrase (optional)</label>
                    <textarea id="seedPassphrase" rows="2" placeholder="Leave empty if none (BIP39 passphrase for additional security)" style="font-family:monospace;resize:vertical;min-height:50px;"></textarea>
                </div>
                <label style="font-size:12px;color:var(--text-muted);display:block;margin-bottom:15px;">
                    <input type="checkbox" id="seedRescan" checked> Rescan blockchain for transactions (recommended)
                </label>
                <button class="btn-secondary" onclick="importSeedPhrase()" style="width:100%;">Import Seed Phrase</button>
                <div id="importSeedStatus" class="download-status" style="margin-top:10px;"></div>
            </div>
        </div>
    </div>
    
    <script>
        let refreshInterval;
        let walletInterval;
        let downloadInterval;
        let nodeRunning = false;
        let settingsLoaded = false;
        let autoStartTriggered = false;
        let binaryFound = false;
        let platformInfo = null;
        let downloadInProgress = false;
        
        // Theme toggle functionality
        function toggleTheme() {
            const body = document.body;
            const btn = document.querySelector('.theme-toggle');
            if (body.getAttribute('data-theme') === 'dark') {
                body.setAttribute('data-theme', 'light');
                btn.textContent = '☀️';
                localStorage.setItem('theme', 'light');
            } else {
                body.setAttribute('data-theme', 'dark');
                btn.textContent = '🌙';
                localStorage.setItem('theme', 'dark');
            }
        }
        
        // Load saved theme on page load
        (function() {
            const savedTheme = localStorage.getItem('theme') || 'dark';
            document.body.setAttribute('data-theme', savedTheme);
            const btn = document.querySelector('.theme-toggle');
            if (btn) btn.textContent = savedTheme === 'dark' ? '🌙' : '☀️';
        })();

        // C3 Security: Helper function for API calls with security token
        function apiCall(url, options = {}) {
            const token = window._RADIANT_SECURITY_TOKEN || '';
            options.headers = options.headers || {};
            options.headers['X-Radiant-Token'] = token;
            return fetch(url, options);
        }

        // Download functionality
        function checkPlatform() {
            apiCall('/api/download/platform')
                .then(r => r.json())
                .then(data => {
                    platformInfo = data;
                    const platformName = document.getElementById('platformName');
                    if (data.detected) {
                        platformName.textContent = data.display_name;
                        if (data.installed) {
                            document.getElementById('downloadPanel').style.display = 'none';
                        }
                    } else {
                        platformName.textContent = data.display_name;
                        platformName.style.color = 'var(--accent-orange)';
                    }
                });
        }
        
        function startDownload() {
            if (downloadInProgress) return;
            
            const btn = document.getElementById('downloadBtn');
            const progress = document.getElementById('downloadProgress');
            const status = document.getElementById('downloadStatus');
            
            btn.disabled = true;
            btn.textContent = 'Downloading...';
            progress.style.display = 'block';
            downloadInProgress = true;
            
            apiCall('/api/download/start', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ platform_key: platformInfo?.platform_key })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    // Start polling for progress
                    downloadInterval = setInterval(checkDownloadProgress, 500);
                } else {
                    status.textContent = 'Error: ' + (data.error || 'Failed to start download');
                    status.className = 'download-status error';
                    btn.disabled = false;
                    btn.textContent = '⬇ Download Binaries';
                    downloadInProgress = false;
                }
            });
        }
        
        function checkDownloadProgress() {
            apiCall('/api/download/progress')
                .then(r => r.json())
                .then(data => {
                    const fill = document.getElementById('progressFill');
                    const status = document.getElementById('downloadStatus');
                    const btn = document.getElementById('downloadBtn');
                    
                    fill.style.width = data.percent + '%';
                    status.textContent = data.message;
                    status.className = 'download-status';
                    
                    if (data.status === 'complete') {
                        clearInterval(downloadInterval);
                        status.className = 'download-status complete';
                        btn.textContent = '✓ Installed';
                        downloadInProgress = false;
                        
                        // Hide download panel after a delay and refresh status
                        setTimeout(() => {
                            document.getElementById('downloadPanel').style.display = 'none';
                            updateStatus();
                        }, 2000);
                    } else if (data.status === 'error') {
                        clearInterval(downloadInterval);
                        status.className = 'download-status error';
                        btn.disabled = false;
                        btn.textContent = '⬇ Retry Download';
                        downloadInProgress = false;
                    }
                });
        }
        
        function switchTab(tab) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            document.querySelector(`.tab[onclick="switchTab('${tab}')"]`).classList.add('active');
            document.getElementById(tab + 'Tab').classList.add('active');
            
            if (tab === 'wallet' && nodeRunning) {
                updateWallet();
            }
        }
        
        function updateStatus() {
            apiCall('/api/status')
                .then(r => r.json())
                .then(data => {
                    const dot = document.getElementById('statusDot');
                    const text = document.getElementById('statusText');
                    const startBtn = document.getElementById('startBtn');
                    const stopBtn = document.getElementById('stopBtn');
                    const statsPanel = document.getElementById('statsPanel');
                    const walletContent = document.getElementById('walletContent');
                    
                    nodeRunning = data.running;
                    
                    if (data.running) {
                        dot.className = 'status-dot running';
                        // Show different status for external vs GUI-started nodes
                        if (data.started_by_gui) {
                            text.textContent = 'Running';
                            stopBtn.disabled = false;
                        } else {
                            text.textContent = 'Running (external)';
                            stopBtn.disabled = true;  // Can't stop external nodes
                        }
                        startBtn.disabled = true;
                        statsPanel.style.display = 'block';
                        walletContent.classList.remove('disabled-overlay');
                        
                        document.getElementById('blockHeight').textContent = 
                            data.blocks ? data.blocks.toLocaleString() : '-';
                        // Show "Synced" if fully synced, otherwise show percentage
                        if (data.synced) {
                            document.getElementById('syncProgress').textContent = '✓ Synced';
                            document.getElementById('syncProgress').style.color = 'var(--accent-green)';
                        } else {
                            document.getElementById('syncProgress').textContent = 
                                data.progress ? data.progress.toFixed(1) + '%' : '-';
                            document.getElementById('syncProgress').style.color = '';
                        }
                        document.getElementById('peerCount').textContent = 
                            data.peers !== undefined ? data.peers : '-';
                        document.getElementById('networkName').textContent = 
                            data.chain || '-';
                    } else {
                        dot.className = 'status-dot';
                        text.textContent = 'Not Running';
                        startBtn.disabled = !data.binary_found;
                        stopBtn.disabled = true;
                        statsPanel.style.display = 'none';
                        walletContent.classList.add('disabled-overlay');
                    }
                    
                    if (!data.binary_found) {
                        text.textContent = 'Binary Not Found';
                        // Show download panel if binaries not found
                        if (!downloadInProgress) {
                            document.getElementById('downloadPanel').style.display = 'block';
                            checkPlatform();
                        }
                    } else {
                        // Hide download panel if binaries are found
                        binaryFound = true;
                        document.getElementById('downloadPanel').style.display = 'none';
                    }
                    
                    // Update settings (only first time)
                    if (data.settings && !settingsLoaded) {
                        settingsLoaded = true;
                        document.getElementById('network').value = data.settings.network || 'mainnet';
                        document.getElementById('datadir').value = data.settings.datadir || '';
                        document.getElementById('autoStart').checked = data.settings.auto_start !== false;
                        document.getElementById('prune').checked = data.settings.prune || false;
                        document.getElementById('pruneSize').value = data.settings.prune_size || 550;
                        document.getElementById('pruneSize').disabled = !data.settings.prune;
                        
                        // Auto-start if enabled and binary found
                        if (data.settings.auto_start !== false && data.binary_found && !data.running && !autoStartTriggered) {
                            autoStartTriggered = true;
                            setTimeout(() => startNode(), 500);
                        }
                    }
                    
                    // Update logs
                    if (data.logs && data.logs.length) {
                        const logDiv = document.getElementById('logOutput');
                        logDiv.innerHTML = data.logs.map(l => 
                            '<div class="log-line">' + escapeHtml(l) + '</div>'
                        ).join('');
                        logDiv.scrollTop = logDiv.scrollHeight;
                    }
                });
        }
        
        function updateWallet() {
            if (!nodeRunning) return;

            apiCall('/api/wallet/info')
                .then(r => r.json())
                .then(data => {
                    if (data.wallet_disabled) {
                        document.getElementById('walletBalance').textContent = 'N/A';
                        document.getElementById('unconfirmedBalance').innerHTML = 
                            '<span style="color:var(--accent-orange);">Wallet not available - node compiled without wallet support.<br>Rebuild with: cmake -DBUILD_RADIANT_WALLET=ON ..</span>';
                        document.getElementById('walletContent').classList.add('wallet-disabled');
                        return;
                    }
                    document.getElementById('walletContent').classList.remove('wallet-disabled');
                    if (data.balance !== undefined) {
                        document.getElementById('walletBalance').textContent = 
                            data.balance.toFixed(8);
                    }
                    if (data.unconfirmed > 0) {
                        document.getElementById('unconfirmedBalance').textContent = 
                            '+' + data.unconfirmed.toFixed(8) + ' RXD unconfirmed';
                    } else {
                        document.getElementById('unconfirmedBalance').textContent = '';
                    }
                });
            
            apiCall('/api/wallet/transactions')
                .then(r => r.json())
                .then(data => {
                    const txList = document.getElementById('txList');
                    if (data.transactions && data.transactions.length) {
                        txList.innerHTML = data.transactions.slice().reverse().map(tx => {
                            const isReceive = tx.category === 'receive';
                            const amount = tx.amount || 0;
                            const conf = tx.confirmations || 0;
                            const time = tx.time ? new Date(tx.time * 1000).toLocaleString() : '';
                            return `<div class="tx-item">
                                <div>
                                    <div class="tx-amount ${isReceive ? 'positive' : 'negative'}">
                                        ${isReceive ? '+' : ''}${amount.toFixed(8)} RXD
                                    </div>
                                    <div class="tx-info">${time}</div>
                                </div>
                                <div class="tx-confirmations">${conf} conf</div>
                            </div>`;
                        }).join('');
                    } else {
                        txList.innerHTML = '<div style="text-align:center;color:#888;padding:20px;">No transactions yet</div>';
                    }
                });
        }
        
        function generateAddress() {
            apiCall('/api/wallet/newaddress', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        document.getElementById('receiveAddress').textContent = data.address;
                    } else {
                        alert('Error: ' + (data.error || 'Failed to generate address'));
                    }
                });
        }
        
        function copyAddress() {
            const addr = document.getElementById('receiveAddress').textContent;
            if (addr && addr !== 'Generate a new address') {
                navigator.clipboard.writeText(addr);
                const box = document.getElementById('receiveAddress');
                box.classList.add('copied');
                setTimeout(() => box.classList.remove('copied'), 1000);
            }
        }
        
        let sendingMax = false;  // Track if user clicked Max button
        
        function setMaxAmount() {
            const feeEstimate = document.getElementById('feeEstimate');
            feeEstimate.textContent = 'Calculating...';
            feeEstimate.style.color = 'var(--text-muted)';
            
            apiCall('/api/wallet/info')
            .then(r => r.json())
            .then(data => {
                if (data.balance !== undefined && data.balance > 0) {
                    // Set full balance - fee will be subtracted by node when sending
                    document.getElementById('sendAmount').value = data.balance.toFixed(8);
                    sendingMax = true;  // Mark that we're sending max
                    feeEstimate.innerHTML = '<span style="color:var(--accent-green);">✓ Sending full balance</span> <span style="color:var(--text-muted);">(fee will be deducted automatically)</span>';
                } else {
                    feeEstimate.textContent = 'No balance available';
                    feeEstimate.style.color = 'var(--accent-red)';
                    sendingMax = false;
                }
            })
            .catch(err => {
                feeEstimate.textContent = 'Error getting balance';
                feeEstimate.style.color = 'var(--accent-red)';
                sendingMax = false;
            });
        }
        
        function sendRXD() {
            const address = document.getElementById('sendAddress').value.trim();
            const amount = document.getElementById('sendAmount').value;
            
            if (!address) {
                alert('Please enter a recipient address');
                return;
            }
            if (!amount || parseFloat(amount) <= 0) {
                alert('Please enter a valid amount');
                return;
            }
            
            let confirmMsg = `Send ${amount} RXD to ${address}?`;
            if (sendingMax) {
                confirmMsg += '\\n\\n(Fee will be deducted from this amount)';
            }
            if (!confirm(confirmMsg)) return;
            
            apiCall('/api/wallet/send', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ address, amount, subtract_fee: sendingMax })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    alert('Transaction sent!\\nTXID: ' + data.txid);
                    document.getElementById('sendAddress').value = '';
                    document.getElementById('sendAmount').value = '';
                    document.getElementById('feeEstimate').textContent = '';
                    sendingMax = false;  // Reset flag
                    updateWallet();
                } else {
                    alert('Error: ' + (data.error || 'Transaction failed'));
                }
            });
        }
        
        // Reset sendingMax flag if user manually edits the amount
        document.addEventListener('DOMContentLoaded', function() {
            const amountInput = document.getElementById('sendAmount');
            if (amountInput) {
                amountInput.addEventListener('input', function() {
                    sendingMax = false;
                    document.getElementById('feeEstimate').textContent = '';
                });
            }
        });
        
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        function getSettings() {
            return {
                network: document.getElementById('network').value,
                datadir: document.getElementById('datadir').value,
                auto_start: document.getElementById('autoStart').checked,
                prune: document.getElementById('prune').checked,
                prune_size: parseInt(document.getElementById('pruneSize').value) || 550
            };
        }
        
        function startNode() {
            document.getElementById('startBtn').disabled = true;
            document.getElementById('statusDot').className = 'status-dot starting';
            document.getElementById('statusText').textContent = 'Starting...';
            
            apiCall('/api/start', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(getSettings())
            })
            .then(r => r.json())
            .then(data => {
                if (!data.success) {
                    alert('Error: ' + data.error);
                }
                updateStatus();
            });
        }
        
        function stopNode() {
            document.getElementById('stopBtn').disabled = true;
            document.getElementById('statusText').textContent = 'Stopping...';
            
            apiCall('/api/stop', {method: 'POST'})
                .then(r => r.json())
                .then(data => {
                    if (!data.success) {
                        alert('Error: ' + data.error);
                    }
                    updateStatus();
                });
        }
        
        function showInfo() {
            document.getElementById('infoModal').classList.add('show');
            document.getElementById('infoContent').textContent = 'Loading...';
            
            apiCall('/api/info')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('infoContent').textContent = 
                        JSON.stringify(data, null, 2);
                });
        }
        
        function closeModal() {
            document.getElementById('infoModal').classList.remove('show');
        }
        
        // Wallet backup/restore functions
        function backupWallet() {
            const status = document.getElementById('backupStatus');
            status.textContent = 'Creating backup...';
            status.className = 'download-status';
            
            apiCall('/api/wallet/backup', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        status.textContent = '✓ ' + data.message;
                        status.className = 'download-status complete';
                        alert('Wallet backed up to:\\n' + data.path);
                    } else {
                        status.textContent = 'Error: ' + data.error;
                        status.className = 'download-status error';
                    }
                });
        }
        
        function showExportKeyModal() {
            document.getElementById('exportKeyModal').classList.add('show');
            document.getElementById('exportedKey').style.display = 'none';
            document.getElementById('exportAddress').value = '';
        }
        
        function closeExportModal() {
            document.getElementById('exportKeyModal').classList.remove('show');
            document.getElementById('exportedKey').style.display = 'none';
        }
        
        function exportPrivkey() {
            const address = document.getElementById('exportAddress').value.trim();
            if (!address) {
                alert('Please enter an address');
                return;
            }
            
            apiCall('/api/wallet/dumpprivkey', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ address })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    document.getElementById('privkeyDisplay').textContent = data.privkey;
                    document.getElementById('exportedKey').style.display = 'block';
                } else {
                    alert('Error: ' + data.error);
                }
            });
        }
        
        function copyPrivkey() {
            const key = document.getElementById('privkeyDisplay').textContent;
            navigator.clipboard.writeText(key);
            const box = document.getElementById('privkeyDisplay');
            box.classList.add('copied');
            setTimeout(() => box.classList.remove('copied'), 1000);
        }
        
        function importPrivkey() {
            const privkey = document.getElementById('importPrivkey').value.trim();
            const rescan = document.getElementById('rescanChain').checked;
            
            if (!privkey) {
                alert('Please enter a private key');
                return;
            }
            
            if (!confirm('Import this private key?' + (rescan ? '\\n\\nNote: Blockchain rescan may take several minutes.' : ''))) {
                return;
            }
            
            const status = document.getElementById('backupStatus');
            status.textContent = 'Importing key' + (rescan ? ' and rescanning blockchain...' : '...');
            status.className = 'download-status';
            
            apiCall('/api/wallet/importprivkey', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ privkey, rescan })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    status.textContent = '✓ ' + data.message;
                    status.className = 'download-status complete';
                    document.getElementById('importPrivkey').value = '';
                    updateWallet();
                } else {
                    status.textContent = 'Error: ' + data.error;
                    status.className = 'download-status error';
                }
            });
        }
        
        // Seed phrase functions
        let currentSeedPhrase = '';
        
        function generateSeedPhrase() {
            const status = document.getElementById('backupStatus');
            status.textContent = 'Generating seed phrase...';
            status.className = 'download-status';
            
            apiCall('/api/wallet/generate-seed', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ words: 12 })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    currentSeedPhrase = data.mnemonic;
                    document.getElementById('seedWords').textContent = data.mnemonic;
                    document.getElementById('seedPhraseDisplay').style.display = 'block';
                    status.textContent = '✓ Seed phrase generated - WRITE IT DOWN!';
                    status.className = 'download-status complete';
                } else {
                    status.textContent = 'Error: ' + data.error;
                    status.className = 'download-status error';
                }
            });
        }
        
        function copySeedPhrase() {
            if (currentSeedPhrase) {
                navigator.clipboard.writeText(currentSeedPhrase);
                alert('Seed phrase copied!\\n\\n⚠️ Remember: Never store digitally - write it on paper!');
            }
        }
        
        function showImportSeedModal() {
            document.getElementById('importSeedModal').classList.add('show');
            document.getElementById('importSeedWords').value = '';
            document.getElementById('seedPassphrase').value = '';
            document.getElementById('importSeedStatus').textContent = '';
        }
        
        function closeImportSeedModal() {
            document.getElementById('importSeedModal').classList.remove('show');
        }
        
        function importSeedPhrase() {
            const mnemonic = document.getElementById('importSeedWords').value.trim().toLowerCase();
            const passphrase = document.getElementById('seedPassphrase').value;
            const rescan = document.getElementById('seedRescan').checked;
            const status = document.getElementById('importSeedStatus');
            
            if (!mnemonic) {
                alert('Please enter your seed phrase');
                return;
            }
            
            const words = mnemonic.split(/\\s+/);
            if (![12, 15, 18, 21, 24].includes(words.length)) {
                alert('Seed phrase must be 12, 15, 18, 21, or 24 words');
                return;
            }
            
            if (!confirm('Import this seed phrase?' + (rescan ? '\\n\\nNote: Blockchain rescan may take several minutes.' : ''))) {
                return;
            }
            
            status.textContent = 'Importing seed phrase' + (rescan ? ' and rescanning...' : '...');
            status.className = 'download-status';
            
            apiCall('/api/wallet/import-seed', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ mnemonic, passphrase, rescan })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    status.textContent = '✓ ' + data.message;
                    status.className = 'download-status complete';
                    setTimeout(() => {
                        closeImportSeedModal();
                        updateWallet();
                    }, 1500);
                } else {
                    status.textContent = 'Error: ' + data.error;
                    status.className = 'download-status error';
                }
            });
        }
        
        function togglePrune() {
            document.getElementById('pruneSize').disabled = 
                !document.getElementById('prune').checked;
        }
        
        // Initial load and start refresh
        updateStatus();
        refreshInterval = setInterval(updateStatus, 3000);
        walletInterval = setInterval(() => {
            if (nodeRunning && document.getElementById('walletTab').classList.contains('active')) {
                updateWallet();
            }
        }, 10000);
        
        // Close modal on escape
        document.addEventListener('keydown', e => {
            if (e.key === 'Escape') closeModal();
        });
    </script>
</body>
</html>
'''


class RequestHandler(http.server.SimpleHTTPRequestHandler):
    # C3 Security: Validate Host header to prevent DNS rebinding attacks
    def _validate_host(self):
        """Reject requests with invalid Host headers (DNS rebinding protection).

        New-2: Only the loopback hostnames bound to the *configured* port are
        accepted -- exact match, no prefix matching. Everything else (e.g.
        evil.com, or loopback on a different port) is rejected.
        """
        host = self.headers.get('Host', '')
        port = self.server.server_address[1]

        # Exact allow-list: loopback host:port for IPv4, name, and IPv6.
        valid_hosts = (
            f'127.0.0.1:{port}',
            f'localhost:{port}',
            f'[::1]:{port}',
        )

        if host not in valid_hosts:
            self.send_error(403, "Invalid Host header")
            return False
        return True

    # C3 Security: Validate security token on API calls
    def _validate_token(self):
        """Validate X-Radiant-Token header matches our per-launch token."""
        global _SECURITY_TOKEN
        token = self.headers.get('X-Radiant-Token', '')
        # New-3: constant-time comparison to avoid timing side-channel.
        if not hmac.compare_digest(str(token), str(_SECURITY_TOKEN)):
            self.send_error(403, "Invalid or missing security token")
            return False
        return True

    def log_message(self, format, *args):
        pass  # Suppress log messages
    
    def do_GET(self):
        # C3 Security: Validate Host header first (DNS rebinding protection)
        if not self._validate_host():
            return

        parsed = urlparse(self.path)

        if parsed.path == "/" or parsed.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            # C3 Security: Add security headers to HTML response
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("X-Frame-Options", "DENY")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; font-src 'self';")
            self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
            self.end_headers()
            # C3 Security: Serve HTML with embedded security token
            self.wfile.write(_get_html_with_token().encode())

        # API endpoints require security token validation
        elif parsed.path == "/api/status":
            if not self._validate_token():
                return
            self.send_json(manager.get_status())

        elif parsed.path == "/api/info":
            if not self._validate_token():
                return
            self.send_json(manager.get_info())

        elif parsed.path == "/api/wallet/info":
            if not self._validate_token():
                return
            self.send_json(manager.get_wallet_info())

        elif parsed.path == "/api/wallet/transactions":
            if not self._validate_token():
                return
            self.send_json(manager.get_transactions())

        elif parsed.path == "/api/wallet/addresses":
            if not self._validate_token():
                return
            self.send_json(manager.get_addresses())
        
        elif parsed.path == "/api/download/platform":
            if not self._validate_token():
                return
            self.send_json(manager.download_manager.get_platform_info())

        elif parsed.path == "/api/download/progress":
            if not self._validate_token():
                return
            self.send_json(manager.download_manager.get_progress())
        
        elif parsed.path == "/logo-light.svg":
            self.serve_logo("RXD_light_logo.svg")
        
        elif parsed.path == "/logo-dark.svg":
            self.serve_logo("RXD_dark_logo.svg")
        
        else:
            self.send_error(404)
    
    def serve_logo(self, filename):
        # Try multiple locations for logo files
        script_dir = os.path.dirname(os.path.abspath(__file__))
        possible_paths = [
            os.path.join(script_dir, "..", "doc", "images", filename),  # Dev mode
            os.path.join(script_dir, "images", filename),  # App bundle
            os.path.join(os.path.dirname(script_dir), "doc", "images", filename),
        ]
        
        # If running as frozen app (PyInstaller), check additional locations
        if getattr(sys, 'frozen', False):
            if hasattr(sys, '_MEIPASS'):
                # PyInstaller bundle
                possible_paths.insert(0, os.path.join(sys._MEIPASS, "images", filename))
            bundle_dir = os.path.dirname(sys.executable)
            possible_paths.insert(0, os.path.join(bundle_dir, "..", "Resources", "images", filename))
        
        content = None
        for path in possible_paths:
            try:
                with open(path, "rb") as f:
                    content = f.read()
                break
            except FileNotFoundError:
                continue
        
        if content is None:
            # Fallback: serve embedded placeholder SVG
            content = FALLBACK_LOGO_SVG.encode()
        
        self.send_response(200)
        self.send_header("Content-Type", "image/svg+xml")
        self.send_header("Cache-Control", "max-age=86400")
        self.end_headers()
        self.wfile.write(content)
    
    def do_POST(self):
        # C3 Security: Validate Host header first (DNS rebinding protection)
        if not self._validate_host():
            return

        # C3 Security: Validate security token on all POST API calls
        if not self._validate_token():
            return

        parsed = urlparse(self.path)

        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode() if content_length else "{}"

        try:
            data = json.loads(body) if body else {}
        except json.JSONDecodeError:
            data = {}

        if parsed.path == "/api/start":
            result = manager.start(data)
            self.send_json(result)
        
        elif parsed.path == "/api/stop":
            result = manager.stop()
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/newaddress":
            label = data.get("label", "")
            result = manager.get_new_address(label)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/send":
            address = data.get("address", "")
            amount = data.get("amount", 0)
            subtract_fee = data.get("subtract_fee", False)
            result = manager.send_rxd(address, amount, subtract_fee)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/maxsend":
            address = data.get("address", "")
            result = manager.get_max_sendable_amount(address)
            self.send_json(result)
        
        elif parsed.path == "/api/download/start":
            platform_key = data.get("platform_key")
            result = manager.download_manager.start_download(platform_key)
            self.send_json(result)
        
        elif parsed.path == "/api/install":
            result = manager.install_to_system()
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/backup":
            destination = data.get("destination")
            result = manager.backup_wallet(destination)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/dumpprivkey":
            address = data.get("address", "")
            result = manager.dump_privkey(address)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/importprivkey":
            privkey = data.get("privkey", "")
            label = data.get("label", "")
            rescan = data.get("rescan", True)
            result = manager.import_privkey(privkey, label, rescan)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/generate-seed":
            words = data.get("words", 12)
            result = manager.generate_seed_phrase(words)
            self.send_json(result)
        
        elif parsed.path == "/api/wallet/import-seed":
            mnemonic = data.get("mnemonic", "")
            passphrase = data.get("passphrase", "")
            rescan = data.get("rescan", True)
            result = manager.import_seed_phrase(mnemonic, passphrase, rescan)
            self.send_json(result)
        
        else:
            self.send_error(404)
    
    def send_json(self, data):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        # C3 Security: Removed Access-Control-Allow-Origin: * (no cross-origin allowed)
        # C3 Security: Add security headers
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; font-src 'self';")
        self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True


def run_browser_mode(port):
    """Run in browser mode (default for Linux)."""
    server = ThreadedHTTPServer(("127.0.0.1", port), RequestHandler)
    
    url = f"http://127.0.0.1:{port}"
    print(f"\n{'='*50}")
    print("  Radiant Core Node")
    print(f"{'='*50}")
    print(f"\n  Opening browser at: {url}")
    print(f"\n  Press Ctrl+C to quit\n")
    print(f"{'='*50}\n")
    
    # Open browser after a short delay
    def open_browser():
        time.sleep(0.5)
        webbrowser.open(url)
    
    threading.Thread(target=open_browser, daemon=True).start()
    
    def signal_handler(sig, frame):
        print("\nShutting down...")
        if manager.is_running:
            manager.stop()
        server.shutdown()
        sys.exit(0)
    
    try:
        signal.signal(signal.SIGINT, signal_handler)
    except (OSError, ValueError):
        pass  # SIGINT may not be available in windowless Windows apps
    try:
        signal.signal(signal.SIGTERM, signal_handler)
    except (OSError, ValueError):
        pass  # SIGTERM not supported on Windows
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if manager.is_running:
            print("Stopping node...")
            manager.stop()
        server.server_close()


def run_windowed_mode(port):
    """Run in windowed mode using pywebview (macOS app)."""
    if not WEBVIEW_AVAILABLE:
        print("Error: pywebview is required for windowed mode.")
        print("Install with: pip install pywebview")
        sys.exit(1)
    
    server = ThreadedHTTPServer(("127.0.0.1", port), RequestHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    
    url = f"http://127.0.0.1:{port}"
    print(f"Starting Radiant Core GUI at {url}")
    
    def on_closed():
        """Called when the window is closed."""
        print("\nShutting down...")
        if manager.is_running:
            manager.stop()
        server.shutdown()
    
    # Create native window
    window = webview.create_window(
        'Radiant Core',
        url,
        width=1200,
        height=800,
        min_size=(800, 600),
        resizable=True
    )
    
    # Start webview (blocks until window is closed)
    webview.start()
    
    # Cleanup after window closes
    on_closed()


def main():
    parser = argparse.ArgumentParser(
        description='Radiant Core Node GUI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python radiant_node_web.py              # Browser mode (default)
  python radiant_node_web.py --windowed   # Native window mode (macOS)
  python radiant_node_web.py --port 9000  # Custom port
        '''
    )
    parser.add_argument(
        '--windowed', '-w',
        action='store_true',
        help='Run in native window mode (requires pywebview)'
    )
    parser.add_argument(
        '--port', '-p',
        type=int,
        default=8765,
        help='Port to run the server on (default: 8765)'
    )
    parser.add_argument(
        '--browser',
        action='store_true',
        help='Force browser mode even on macOS app bundle'
    )
    
    args = parser.parse_args()
    
    # Detect if running as macOS app bundle
    is_app_bundle = getattr(sys, 'frozen', False) and platform.system() == 'Darwin'
    
    # Determine mode: windowed if --windowed flag or running as app bundle (unless --browser)
    use_windowed = (args.windowed or is_app_bundle) and not args.browser
    
    if use_windowed:
        run_windowed_mode(args.port)
    else:
        run_browser_mode(args.port)


if __name__ == "__main__":
    main()
