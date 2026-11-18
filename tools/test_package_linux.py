"""The Linux package's one-module check (tools/package_linux.py foreign_libraries).

Run: python3 -B tools/test_package_linux.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_linux import foreign_libraries  # noqa: E402

SYSTEM_LDD = """\tlinux-vdso.so.1 (0x00007ffd2b9f3000)
\tlibX11.so.6 => /lib/x86_64-linux-gnu/libX11.so.6 (0x00007f0e1c000000)
\tlibm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007f0e1bf00000)
\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f0e1bc00000)
\t/lib64/ld-linux-x86-64.so.2 (0x00007f0e1c200000)
"""
SYSTEM_READELF = """ 0x0000000000000001 (NEEDED)             Shared library: [libX11.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
"""


class ForeignLibraries(unittest.TestCase):
    def test_system_only_passes(self):
        self.assertEqual(foreign_libraries(SYSTEM_READELF, SYSTEM_LDD), [])

    def test_shared_cxx_runtime_is_refused(self):
        ldd = SYSTEM_LDD + "\tlibc++.so.1 => /usr/lib/llvm-22/lib/libc++.so.1 (0x00007f0e1b000000)\n"
        readelf = SYSTEM_READELF + " 0x0000000000000001 (NEEDED)  Shared library: [libc++abi.so.1]\n"
        self.assertEqual(foreign_libraries(readelf, ldd), ["libc++abi.so.1", "libc++.so.1"])

    def test_gui_forms_library_is_refused(self):
        ldd = SYSTEM_LDD + "\tlibgui_forms_application.so => /opt/app/libgui_forms_application.so (0x00007f0e1b000000)\n"
        self.assertEqual(foreign_libraries(SYSTEM_READELF, ldd), ["libgui_forms_application.so"])

    def test_unresolved_and_foreign_paths_are_refused(self):
        ldd = SYSTEM_LDD + "\tlibunknown.so.3 => not found\n\tlibfoo.so.1 => /opt/foo/libfoo.so.1 (0x00007f0e1b000000)\n"
        self.assertEqual(foreign_libraries(SYSTEM_READELF, ldd), ["libunknown.so.3", "libfoo.so.1"])


if __name__ == "__main__":
    unittest.main()
