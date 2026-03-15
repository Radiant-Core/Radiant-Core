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
APP_VERSION = '2.1.2'
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
]

# Icon file path
ICON_FILE = os.path.join(script_dir, '..', 'doc', 'images', 'RXDCore.icns')

# py2app options
OPTIONS = {
    'argv_emulation': False,  # Don't emulate argv (we handle args ourselves)
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
    'packages': ['webview'],  # Include pywebview
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
    setup_requires=['py2app'],
)
