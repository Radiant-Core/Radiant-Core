#!/usr/bin/env python3
"""
Regression tests for the fail-CLOSED archive extraction in radiant_node_web.py
(audit finding: "release-download verification fails open").

Before the fix, the auto-downloader relied on tarfile/zipfile ``filter='data'``
(Python 3.12+) and silently fell back to an UNFILTERED ``extractall`` on the
README-supported Python 3.6-3.11. That is a fail-open path: a malicious archive
with ``../`` members (CVE-2007-4559 "tar/zip slip") would write outside the
extraction root.

These tests run on every supported Python (incl. < 3.12). With the OLD code,
``test_*_slip_*`` would FAIL (the file escapes the destination). With the
fix, ``safe_extract_zip`` / ``safe_extract_tar`` raise instead, and a benign
archive still extracts correctly.

Run:  python3 gui/test_safe_extract.py
"""

import io
import os
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from radiant_node_web import safe_extract_zip, safe_extract_tar  # noqa: E402


class SafeExtractTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.dest = os.path.join(self.root, "extract")
        os.makedirs(self.dest, exist_ok=True)
        # Sentinel target an attacker would try to overwrite, OUTSIDE dest.
        self.outside = os.path.join(self.root, "PWNED")

    def tearDown(self):
        self._tmp.cleanup()

    # ---- zip ----------------------------------------------------------------

    def _make_zip(self, members):
        path = os.path.join(self.root, "a.zip")
        with zipfile.ZipFile(path, "w") as zf:
            for name, data in members:
                zf.writestr(name, data)
        return path

    def test_zip_benign_extracts(self):
        path = self._make_zip([("radiant-core/radiantd", b"binary")])
        with zipfile.ZipFile(path) as zf:
            safe_extract_zip(zf, self.dest)
        self.assertTrue(
            os.path.exists(os.path.join(self.dest, "radiant-core", "radiantd")))

    def test_zip_slip_relative_is_rejected(self):
        path = self._make_zip([("../PWNED", b"owned")])
        with zipfile.ZipFile(path) as zf:
            with self.assertRaises(RuntimeError):
                safe_extract_zip(zf, self.dest)
        self.assertFalse(os.path.exists(self.outside))

    def test_zip_slip_absolute_is_rejected(self):
        # Absolute member name pointing at a sibling path.
        evil = os.path.join(self.root, "PWNED_ABS")
        path = self._make_zip([(evil.lstrip("/"), b"owned")])
        # Rewrite with a truly absolute name (zipfile strips leading slash on
        # write, so craft the member name directly).
        path = os.path.join(self.root, "abs.zip")
        with zipfile.ZipFile(path, "w") as zf:
            zi = zipfile.ZipInfo(filename="/" + evil.lstrip("/"))
            zf.writestr(zi, b"owned")
        with zipfile.ZipFile(path) as zf:
            with self.assertRaises(RuntimeError):
                safe_extract_zip(zf, self.dest)
        self.assertFalse(os.path.exists(evil))

    # ---- tar ----------------------------------------------------------------

    def _make_tar(self, build):
        path = os.path.join(self.root, "a.tar.gz")
        with tarfile.open(path, "w:gz") as tf:
            build(tf)
        return path

    def test_tar_benign_extracts(self):
        def build(tf):
            data = b"binary"
            ti = tarfile.TarInfo("radiant-core/radiantd")
            ti.size = len(data)
            tf.addfile(ti, io.BytesIO(data))
        path = self._make_tar(build)
        with tarfile.open(path) as tf:
            safe_extract_tar(tf, self.dest)
        self.assertTrue(
            os.path.exists(os.path.join(self.dest, "radiant-core", "radiantd")))

    def test_tar_slip_relative_is_rejected(self):
        def build(tf):
            data = b"owned"
            ti = tarfile.TarInfo("../PWNED")
            ti.size = len(data)
            tf.addfile(ti, io.BytesIO(data))
        path = self._make_tar(build)
        with tarfile.open(path) as tf:
            with self.assertRaises(RuntimeError):
                safe_extract_tar(tf, self.dest)
        self.assertFalse(os.path.exists(self.outside))

    def test_tar_symlink_escape_is_rejected(self):
        def build(tf):
            ti = tarfile.TarInfo("link")
            ti.type = tarfile.SYMTYPE
            ti.linkname = "../PWNED"
            tf.addfile(ti)
        path = self._make_tar(build)
        with tarfile.open(path) as tf:
            with self.assertRaises(RuntimeError):
                safe_extract_tar(tf, self.dest)
        self.assertFalse(os.path.exists(self.outside))

    def test_tar_device_member_is_rejected(self):
        def build(tf):
            ti = tarfile.TarInfo("dev/null")
            ti.type = tarfile.CHRTYPE
            ti.devmajor = 1
            ti.devminor = 3
            tf.addfile(ti)
        path = self._make_tar(build)
        with tarfile.open(path) as tf:
            with self.assertRaises(RuntimeError):
                safe_extract_tar(tf, self.dest)


if __name__ == "__main__":
    unittest.main(verbosity=2)
