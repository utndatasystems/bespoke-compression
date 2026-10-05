"""End-to-end source submission and lifecycle acceptance tests, using real native code."""
import json
import time
from pathlib import Path

from test_interface import InterfaceTests, COPY_CODEC


class WorkflowTests(InterfaceTests):
    # Reuse fixture helpers, not the inherited first-slice tests.
    def source_manifest(self, name='source', nondeterministic=False, unsafe=False):
        folder = self.lab.public / 'work' / name
        folder.mkdir()
        code = COPY_CODEC.replace('ids[i]+1>=ends.size()', 'ids[i]>=ends.size()-1')
        if unsafe:
            code = code.replace('if(s.n>c)return -1;', 'if(s.n>c){o[c]=1;return -1;}')
        (folder / 'copy.cpp').write_text(code)
        commands = []
        for role in ('encoder', 'decoder'):
            commands.append(['g++', '-std=c++17', '-O2', '-fPIC', '-shared', '-I/source/interface',
                             '/source/copy.cpp', '-o', f'/output/{role}.so'] + (['-DDECODER'] if role == 'decoder' else []))
        if nondeterministic:
            commands.append(['python3', '-c', "import os; open('/output/decoder.so','ab').write(os.urandom(16))"])
        (folder / 'build.json').write_text(json.dumps({'name': name, 'variant': 'rows',
            'sources': ['copy.cpp'], 'commands': commands, 'encoder': 'encoder.so', 'decoder': 'decoder.so'}))
        return name + '/build.json'

    def test_source_to_final_submission_and_artifact_integrity(self):
        manifest = self.source_manifest()
        build = self.wait(self.lab.build(manifest, request_id='build'))
        self.assertEqual(build['status'], 'complete', build)
        b = self.lab.result(build['result_id'])
        self.assertTrue(b['reproducible'])
        self.assertEqual(build['job_id'], self.lab.build(manifest, request_id='build')['job_id'])
        measured = self.wait(self.lab.evaluate(b['result_id'], request_id='full'))
        self.assertEqual(measured['status'], 'complete', measured)
        validated = self.wait(self.lab.validate(measured['result_id'], request_id='validate'))
        self.assertEqual(validated['status'], 'complete', validated)
        v = self.lab.result(validated['result_id'])
        self.assertTrue(v['passed'], v)
        self.assertTrue(all(g['status'] == 'passed' for g in v['gates']))
        self.assertGreater(v['case_count'], 10)
        self.assertEqual(self.lab.compare([measured['result_id'], measured['result_id']])['scoring']['primary_score'], None)
        listing = self.lab.artifact(b['result_id'])
        self.assertIn('source/copy.cpp', [f['path'] for f in listing['files']])
        self.assertIn('lab_encode', self.lab.artifact(b['result_id'], 'source/copy.cpp')['content'])
        receipt = self.lab.finish(measured['result_id'], validated['result_id'], request_id='finish')
        self.assertEqual(receipt['status'], 'submitted_for_owner_review')
        self.assertFalse(receipt['target_claim_verified'])
        self.assertEqual(receipt, self.lab.finish(measured['result_id'], validated['result_id'], request_id='finish'))
        with self.assertRaises(ValueError):
            self.lab.artifact(b['result_id'], '../../config.json')

    def test_nondeterminism_and_real_asan_fault_cannot_qualify(self):
        bad = self.wait(self.lab.build(self.source_manifest('random', nondeterministic=True), request_id='random'))
        self.assertEqual(bad['status'], 'failed', bad)
        self.assertFalse(self.lab.result(bad['result_id'])['reproducible'])
        built = self.wait(self.lab.build(self.source_manifest('unsafe', unsafe=True), request_id='unsafe'))
        self.assertEqual(built['status'], 'complete', built)
        measured = self.wait(self.lab.evaluate(built['result_id'], request_id='unsafe-measure'))
        self.assertEqual(measured['status'], 'complete', measured)
        checked = self.wait(self.lab.validate(measured['result_id'], request_id='unsafe-validate'))
        self.assertEqual(checked['status'], 'failed', checked)
        self.assertFalse(self.lab.result(checked['result_id'])['passed'])
        with self.assertRaises(ValueError):
            self.lab.finish(measured['result_id'], checked['result_id'], request_id='bad-finish')

    def test_cancel_build_preserves_receipt_and_does_not_relaunch(self):
        manifest = self.source_manifest('slow')
        path = self.lab.public / 'work' / manifest
        data = json.loads(path.read_text()); data['commands'].insert(0, ['sleep', '40'])
        path.write_text(json.dumps(data))
        job = self.lab.build(manifest, request_id='slow')
        deadline = time.monotonic() + 10
        while self.lab.status(job['job_id'])['status'] == 'queued' and time.monotonic() < deadline:
            time.sleep(.05)
        self.lab.cancel(job['job_id'])
        state = self.wait(job)
        self.assertEqual(state['status'], 'cancelled', state)
        self.assertEqual(self.lab.build(manifest, request_id='slow')['status'], 'cancelled')


# The parent supplies fixture helpers; keep its original tests in test_interface.
for _name in list(InterfaceTests.__dict__):
    if _name.startswith('test_'):
        setattr(WorkflowTests, _name, None)
