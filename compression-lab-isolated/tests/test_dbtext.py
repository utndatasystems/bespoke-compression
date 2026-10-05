"""Real native acceptance tests for the shared-archive DBText contract."""
import copy
import json
import os
import tempfile
from pathlib import Path

from test_workflow import WorkflowTests
from lab_interface.api import initialize
from lab_interface.scoring import seal, score


class DBTextTests(WorkflowTests):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='lab-dbtext-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / 'strings'
        self.input.write_bytes(b''.join((f'row {i}: ' + 'abc' * (i % 9) + '\n').encode() for i in range(203)))
        self.lab = initialize(self.root/'lab', [self.input], host_lock=self.root / "host-benchmark.lock", cpu=min(os.sched_getaffinity(0)), server_local=os.environ.get("LAB_TEST_SERVER_LOCAL")=="1")
        seal(self.lab, target=dict(maximum_package_bytes=1000000, minimum_decode_MB_s=0.00001),
             hardware={}, dbtext_target=dict(maximum_package_bytes=1000000,
                 minimum_rows_M_s={str(p):0.00001 for p in (1,3,10,30,100)}))

    def build_candidate(self, name='copy', change=None):
        manifest = self.source_manifest(name)
        source = self.lab.public/'work'/name/'copy.cpp'
        if change:
            source.write_text(change(source.read_text()))
        built = self.wait(self.lab.build(manifest, request_id=name+'-build'))
        self.assertEqual(built['status'], 'complete', built)
        return built['result_id']

    def test_public_contract_has_no_conflicting_row_protocol(self):
        protocol=self.lab.environment()
        self.assertEqual(protocol['evaluation_tool'],'evaluate_dbtext')
        self.assertEqual(protocol['candidate_manifest']['variants'],['rows'])
        self.assertEqual(protocol['measurement']['rows'],protocol['dbtext'])
        self.assertNotIn('ceiling',json.dumps(protocol['measurement']))
        self.assertEqual(protocol['measurement']['bulk']['trials'],7)

    def test_joint_lifecycle_and_no_bulk_only_success(self):
        build = self.build_candidate()
        with self.assertRaisesRegex(ValueError, 'evaluate_dbtext'):
            self.lab.evaluate(build, request_id='wrong-endpoint')
        measured = self.wait(self.lab.evaluate_dbtext(build, request_id='joint'))
        self.assertEqual(measured['status'], 'complete', measured)
        row = self.lab.result(measured['result_id'])
        joint = row['measurements']['dbtext']
        self.assertTrue(joint['same_configuration'])
        self.assertTrue(joint['byte_exact'])
        self.assertEqual(joint['replays'], 3)
        self.assertEqual(joint['warmup_calls'], 100)
        self.assertEqual(joint['timed_calls'], 100)
        self.assertEqual(set(joint['selective']), {'1','3','10','30','100'})
        self.assertGreater(joint['validated_queries'], 15)
        self.assertTrue(row['scoring']['target_met'])
        policy = self.lab.config['protocol']['scoring']
        for mutation in ('missing_rows', 'wrong_archive', 'slow_rows', 'quick'):
            bad = copy.deepcopy(row)
            if mutation == 'missing_rows': bad['measurements'].pop('dbtext')
            if mutation == 'wrong_archive': bad['measurements']['dbtext']['same_configuration'] = False
            if mutation == 'slow_rows': bad['measurements']['dbtext']['selective']['3']['median_rows_M_s'] = 0
            if mutation == 'quick': bad['depth'] = 'quick'
            self.assertFalse(score(bad, policy)['target_met'], mutation)
        validated = self.wait(self.lab.validate(row['result_id'], request_id='joint-validation'))
        self.assertEqual(validated['status'], 'complete', validated)
        v = self.lab.result(validated['result_id'])
        self.assertTrue(any(g['name']=='selected_rows_capacity_and_memory' and g['status']=='passed' for g in v['gates']))
        receipt = self.lab.finish(row['result_id'], v['result_id'], request_id='done')
        self.assertTrue(receipt['measurement_target_met'])
        self.assertFalse(receipt['target_claim_verified'])

    def test_wrong_row_boundaries_and_missing_function_fail(self):
        for name, change in (
            ('bad_offsets', lambda s:s.replace('offsets[i+1]=n;', 'offsets[i+1]=0;')),
            ('missing_rows', lambda s:s.replace('lab_rows(', 'not_lab_rows(')),
        ):
            build = self.build_candidate(name, change)
            state = self.wait(self.lab.evaluate_dbtext(build, request_id=name+'-measure', quick=True))
            self.assertEqual(state['status'], 'failed', state)
            if state.get('result_id'):
                self.assertFalse(self.lab.result(state['result_id'])['scoring']['target_met'])

    def test_row_only_memory_fault_cannot_finish(self):
        build=self.build_candidate('unsafe_rows',lambda s:s.replace('size_t n=0;offsets[0]=0;',
            'if(c==0)o[0]=1; size_t n=0;offsets[0]=0;'))
        measured=self.wait(self.lab.evaluate_dbtext(build,request_id='unsafe-full'))
        self.assertEqual(measured['status'],'complete',measured)
        checked=self.wait(self.lab.validate(measured['result_id'],request_id='unsafe-check'))
        self.assertEqual(checked['status'],'failed',checked)
        row=self.lab.result(checked['result_id'])
        self.assertFalse(row['passed'])
        self.assertTrue(any(g['name']=='selected_rows_capacity_and_memory' and g['status']=='failed' for g in row['gates']))
        with self.assertRaises(ValueError):
            self.lab.finish(measured['result_id'],checked['result_id'],request_id='no-finish')


for _name in dir(WorkflowTests):
    if _name.startswith('test_'):
        setattr(DBTextTests, _name, None)
