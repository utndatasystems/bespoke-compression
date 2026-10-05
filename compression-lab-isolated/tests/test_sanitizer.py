"""Real sanitizer diagnostics must fail even when UBSan recovers with exit zero."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from lab_interface.remote_worker import secure_execute
from lab_interface.workflow import build_commands


PROBE = r'''
#include <cstdint>
// Match the diagnostic harness: leak scanning needs unavailable proc/ptrace access.
extern "C" int __lsan_is_turned_off(){return 1;}
extern "C" const char* __asan_default_options(){return "detect_leaks=0:abort_on_error=1:symbolize=0";}
int main(int argc, char**) {
 alignas(8) char bytes[16]={};
 // With no argument, deliberately perform the same class of invalid packed load.
 volatile const uint32_t* pointer=reinterpret_cast<const uint32_t*>(bytes+(argc==1));
 volatile uint32_t value=*pointer;
 return value;
}
'''


class SanitizerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='lab-sanitizer-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cpu = min(os.sched_getaffinity(0))
        self.source = self.root/'probe.cpp'
        self.source.write_text(PROBE)

    def execute_probe(self, binary, label, *, clean=False):
        listing = subprocess.check_output(['ldd', str(binary)], text=True)
        libraries = sorted({str(Path(p).resolve()) for p in re.findall(r'(/[^\s()]+)', listing)})
        return secure_execute([str(binary), *(['clean'] if clean else [])], self.root/label,
                              [str(binary), *libraries, '/dev/null'], libraries, self.cpu,
                              sanitizer=True, timeout=10)

    def test_recovered_diagnostic_fails_and_clean_control_passes(self):
        binary = self.root/'recover'
        subprocess.run(['g++', '-O1', '-g', '-fsanitize=address,undefined',
                        str(self.source), '-o', str(binary)], check=True,
                       capture_output=True, text=True, timeout=30)
        # UBSan cannot read its environment through the sandbox's restricted proc.
        # Deliberately retain recovery mode to test the verdict independently.
        recovered = self.execute_probe(binary, 'recovered')
        self.assertEqual(recovered['returncode'], 0, recovered)
        self.assertIn('runtime error: load of misaligned address', recovered['stderr'])
        self.assertEqual(recovered['reason_code'], 'sanitizer_diagnostic', recovered)
        clean = self.execute_probe(binary, 'clean', clean=True)
        self.assertEqual(clean['returncode'], 0, clean)
        self.assertIsNone(clean['reason_code'], clean)
        self.assertEqual(clean['stderr'], '', clean)

    def test_diagnostic_build_cannot_recover_from_undefined_behavior(self):
        job = self.root/'job'
        (job/'source').mkdir(parents=True)
        (job/'source/probe.cpp').write_text(PROBE)
        output = job/'diagnostic-build'
        build_commands(SimpleNamespace(config={'cpu':self.cpu,'build_cpu':self.cpu,'server_local':os.environ.get('LAB_TEST_SERVER_LOCAL')=='1','proot':__import__('lab_interface.server_shell',fromlist=['proot_path']).proot_path()}), job,
                       {'commands': [['g++', '-O1', '/source/probe.cpp', '-o', '/output/probe']]},
                       output, sanitizer=True)
        failed = self.execute_probe(output/'probe', 'fatal')
        self.assertNotEqual(failed['returncode'], 0, failed)
        self.assertIn('runtime error: load of misaligned address', failed['stderr'])
        self.assertEqual(failed['reason_code'], 'sanitizer_diagnostic', failed)


if __name__ == '__main__':
    unittest.main()
