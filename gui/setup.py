"""
py2app setup script for Radiant Core GUI macOS application.

Usage:
    pip install py2app pywebview
    python setup.py py2app

This creates a standalone macOS .app bundle in the dist/ folder.
"""

from setuptools import setup
import sys
import os

# Get the directory containing this script
script_dir = os.path.dirname(os.path.abspath(__file__))

# Application metadata
APP_NAME = 'Radiant Core'
APP_VERSION = '3.1.2'
APP_BUNDLE_ID = 'org.radiantblockchain.radiant-core-gui'

# Main application script
APP = ['radiant_node_web.py']

# Additional data files to include
DATA_FILES = [
    ('', ['bip39.py']),  # Include BIP39 module in app bundle
    ('images', [
        '../doc/images/RXD_light_logo.svg',
        '../doc/images/RXD_dark_logo.svg',
    ]),  # Logo images
    ('binaries/radiant-core-macos-arm64', [
        'binaries/radiant-core-macos-arm64/radiantd',
        'binaries/radiant-core-macos-arm64/radiant-cli',
        'binaries/radiant-core-macos-arm64/radiant-tx',
    ]),  # Include macOS ARM64 binaries
    ]

# Icon file path
ICON_FILE = os.path.join(script_dir, '..', 'doc', 'images', 'RXDCore.icns')

# py2app options
OPTIONS = {
    'argv_emulation': False,  # pywebview initialises NSApplication; argv_emulation's
                              # Carbon AE event loop conflicts with that. PSN args are
                              # stripped at the Python level in main() instead.
    'iconfile': ICON_FILE if os.path.exists(ICON_FILE) else None,
    'plist': {
        'CFBundleName': APP_NAME,
        'CFBundleDisplayName': APP_NAME,
        'CFBundleIdentifier': APP_BUNDLE_ID,
        'CFBundleVersion': APP_VERSION,
        'CFBundleShortVersionString': APP_VERSION,
        'LSMinimumSystemVersion': '10.15.0',
        'NSHighResolutionCapable': True,
        'NSRequiresAquaSystemAppearance': False,  # Support dark mode
        'CFBundleDocumentTypes': [],
        'LSApplicationCategoryType': 'public.app-category.utilities',
        'NSHumanReadableCopyright': '© 2024-2026 Radiant Blockchain. MIT License.',
    },
    'packages': [
        'webview',      # pywebview core — py2app auto-discovers the PyObjC
                        # (objc, Foundation, AppKit, WebKit) deps by following
                        # webview's own imports. Listing them explicitly caused
                        # double-initialisation and NSApplication conflicts.
    ],
    'includes': [
        'http.server',
        'socketserver',
        'threading',
        'json',
        'hashlib',
        'tarfile',
        'zipfile',
        'webbrowser',
        'urllib.request',
        'urllib.parse',
        'urllib.error',
        'pathlib',
        'platform',
        'subprocess',
        'signal',
        'argparse',
    ],
    'excludes': [
        'tkinter',
        'matplotlib',
        'numpy',
        'pandas',
        'scipy',
        'PIL',
        'cv2',
        'setuptools',  # Avoid vendored package conflicts
    ],
    'resources': [],
    'frameworks': [],
}

# Remove None iconfile if not present
if OPTIONS['iconfile'] is None:
    del OPTIONS['iconfile']

setup(
    name=APP_NAME,
    version=APP_VERSION,
    app=APP,
    data_files=DATA_FILES,
    options={'py2app': OPTIONS},
    # Do NOT use setup_requires=['py2app']. With setuptools>=70 the legacy
    # fetch_build_eggs bootstrap path tries to import pkg_resources from
    # within setuptools' own finalize_options(), before setuptools is fully
    # initialised, causing a circular ModuleNotFoundError. py2app is installed
    # into the venv by pip before setup.py runs, so it is already available as
    # a distutils command via its entry_point — setup_requires is unnecessary.
)
