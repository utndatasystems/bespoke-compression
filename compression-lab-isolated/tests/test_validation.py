"""Regression checks for recoverable sanitizer failures in validation receipts."""
import contextlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from lab_interface import validation
import test_dbtext


# Same diagnostic class and source location as the retained Astra diagnostics.
UBSAN_DIAGNOSTIC = (
    "/source/textcodec128_preload.h:26:9: runtime error: member access within "
    "misaligned address 0x000000000001 for type 'const struct Header', "
    "which requires 8 byte alignment\n"
)


class ValidationReceiptTests(unittest.TestCase):
    def validate_records(self, records, *, joint=True):
        with tempfile.TemporaryDirectory(prefix='lab-validation-receipts-') as temp:
            root = Path(temp)
            owner = root/'owner'
            public = root/'public'
            source = owner/'jobs/build/source'
            source.mkdir(parents=True)
            (source/'codec.cpp').write_text('// fixture; compilation is mocked\n')
            (source.parent/'build-spec.json').write_text(json.dumps({
                'sources': ['codec.cpp'], 'commands': [['g++', 'codec.cpp']],
                'decoder': 'decoder.so', 'encoder': 'encoder.so',
            }))
            (public/'inputs').mkdir(parents=True)
            (public/'inputs/column').write_bytes(b'row\n')
            job = owner/'jobs/validation'
            job.mkdir()
            returned = root/'returned'
            returned.mkdir()
            (returned/'CALLS.json').write_text(json.dumps(records))
            values = {
                'measurement': {'build_id': 'build', 'job_id': 'measurement', 'result_id': 'measurement'},
                'build': {'job_id': 'build', 'result_id': 'build'},
            }
            lab = SimpleNamespace(owner=owner, public=public, result=values.__getitem__,
                config={'remote': {'transport': 'fixture'}, 'protocol': {'dbtext': {}} if joint else {},
                        'columns': [{'name': 'column', 'sha256': 'fixture', 'bytes': 4, 'rows': 1}]})
            with contextlib.ExitStack() as stack:
                stack.enter_context(patch.object(validation, 'build_commands'))
                stack.enter_context(patch.object(validation.strings, 'config', return_value={'libraries': {}}))
                stack.enter_context(patch.object(validation.candidate, 'libraries', return_value={}))
                stack.enter_context(patch.object(validation.candidate, 'elf', return_value={'interpreter': None, 'needed': []}))
                stack.enter_context(patch('lab_interface.remote.diagnostics', return_value=returned))
                checks = stack.enter_context(patch('lab_interface.dbtext.check_queries', return_value=[]))
                result = validation.validate(lab, job, {'source_result_id': 'measurement'})
            return result, checks.call_count

    @staticmethod
    def records(count=17):
        return [{'returncode': 0, 'reason_code': None, 'stderr': '', 'stdout': '{}'} for _ in range(count)]

    def assert_sanitizer_failure(self, result):
        self.assertFalse(result['passed'], result)
        self.assertEqual(result['failure']['code'], 'sanitizer_diagnostic', result)
        self.assertEqual(result['failure']['returncode'], 0, result)
        self.assertIn(UBSAN_DIAGNOSTIC.strip(), result['failure']['stderr'])

    def test_zero_exit_bulk_ubsan_rejects_first_offending_record(self):
        records = self.records()
        records[3]['stderr'] = UBSAN_DIAGNOSTIC
        result, checked_rows = self.validate_records(records)
        self.assert_sanitizer_failure(result)
        self.assertEqual(checked_rows, 0)

    def test_zero_exit_row_ubsan_cannot_pass(self):
        records = self.records()
        records[16]['stderr'] = UBSAN_DIAGNOSTIC
        result, checked_rows = self.validate_records(records)
        self.assert_sanitizer_failure(result)
        self.assertEqual(checked_rows, 0)

    def test_clean_complete_bulk_and_joint_receipts_pass(self):
        for joint, count in ((False, 16), (True, 17)):
            with self.subTest(joint=joint):
                result, checked_rows = self.validate_records(self.records(count), joint=joint)
                self.assertTrue(result['passed'], result)
                self.assertEqual(checked_rows, int(joint))

    def test_missing_or_extra_receipts_cannot_pass(self):
        for joint, count in ((False, 15), (False, 17), (True, 15), (True, 16), (True, 18)):
            with self.subTest(joint=joint, count=count):
                result, _ = self.validate_records(self.records(count), joint=joint)
                self.assertFalse(result['passed'], result)


class RowUndefinedBehaviorTests(test_dbtext.DBTextTests):
    def test_real_row_only_misaligned_load_cannot_finish(self):
        def misalign(source):
            return source.replace('size_t n=0;offsets[0]=0;', '''
 alignas(8) unsigned char probe[8]={1,2,3,4,5,6,7,8};
 if(c==0 && count){
  volatile uint32_t value=*reinterpret_cast<volatile uint32_t*>(probe+1);
  (void)value;
 }
 size_t n=0;offsets[0]=0;''')
        build = self.build_candidate('row_ubsan', misalign)
        measured = self.wait(self.lab.evaluate_dbtext(build, request_id='row-ubsan-measure'))
        self.assertEqual(measured['status'], 'complete', measured)
        checked = self.wait(self.lab.validate(measured['result_id'], request_id='row-ubsan-validate'))
        self.assertEqual(checked['status'], 'failed', checked)
        result = self.lab.result(checked['result_id'])
        self.assertFalse(result['passed'], result)
        self.assertTrue(any(g['name'] == 'selected_rows_capacity_and_memory' and
                            g['status'] == 'failed' for g in result['gates']), result)
        with self.assertRaises(ValueError):
            self.lab.finish(measured['result_id'], checked['result_id'], request_id='row-ubsan-finish')


# Reuse only the native fixtures, without rerunning inherited integration tests.
for _name in dir(test_dbtext.DBTextTests):
    if _name.startswith('test_'):
        setattr(RowUndefinedBehaviorTests, _name, None)


if __name__ == '__main__':
    unittest.main()
