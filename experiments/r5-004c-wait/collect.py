"""Read-only evidence interpretation, not a new native experiment."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line] if path.exists() else []


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def fixed_store(root):
    data = (root / 'session.fvs').read_bytes()
    assert hashlib.sha256(data[:-32]).digest() == data[-32:]
    at = 32 + 4 * 12 + 32

    def text():
        nonlocal at
        size, = struct.unpack_from('<I', data, at)
        at += 4
        assert size <= 128 and at + size <= len(data) - 32
        result = data[at:at + size].decode('utf-8')
        at += size
        return result

    text()  # client identifier
    expected = []
    for label in ('S0', 'SA', 'SB'):
        expected.append((root / (label + '.fvs'), text()))
        text()  # immutable in-memory fingerprint
    for _ in range(2):
        identity, view, action, content, manifest = [text() for _ in range(5)]
        assert identity in ('A-v1', 'B-v1') and view and action
        expected += [(root / 'artifacts' / identity / 'data.bin', content), (root / 'artifacts' / identity / 'manifest.json', manifest)]
        at += 4 + 4 + 8
    assert at == len(data) - 32
    return [dict(file=str(path.relative_to(root)), expected=value, actual=sha(path), passed=sha(path) == value) for path, value in expected]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--regression', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = []
    paths = [('baseline1/restore', 'RB/cleanup-diagnostic.jsonl'), ('trim-checkpoint1', 'trim/cleanup-diagnostic.jsonl'),
             ('trim-normal2', 'normal-diagnostic.jsonl'), ('trim-checkpoint2', 'trim/cleanup-diagnostic.jsonl'),
             ('profile-normal', 'normal-diagnostic.jsonl'), ('profile-checkpoint', 'trim/cleanup-diagnostic.jsonl'),
             ('final-pair/normal', 'normal-diagnostic.jsonl'), ('final-pair/checkpoint', 'trim/cleanup-diagnostic.jsonl')]
    for relative, filename in paths:
        root = args.root / relative
        trace = rows(root / 'manager' / filename)
        controller = rows(root / 'manager/controller.jsonl')
        queries = [x for x in trace if x['type'] == 'wait_chain']
        nodes = [x for x in trace if x['type'] == 'wait_node']
        grouped = [dict(tid=query['tid'], result=query['result'], error=query['error'], cycle=query['cycle'], nodes=[x for x in nodes if x['queryTid'] == query['tid']]) for query in queries]
        callbacks = [x for x in trace if x['type'] == 'wct_com_callbacks']
        case = dict(case=relative, supervisor=read(root / 'supervisor/summary.json'), callbacks=callbacks, chains=grouped,
                    failures=[x for x in controller if x['type'] in ('trim_primary_failure', 'abort_native_window_cleanup', 'trim_complete')],
                    threadObservations=[x for x in controller if x['type'] == 'trim_thread_observation'],
                    frames=[x for x in trace if x['type'] == 'frame'])
        case['workerCOMSuccessorObserved'] = len(queries) >= 2 and any(x['kind'] == 5 for x in nodes) and any(x['kind'] == 8 and x['status'] == 5 and x['tid'] == 0 for x in nodes)
        assert case['supervisor']['protocolPassed'] and case['supervisor']['cleanupVerified']
        cases.append(case)
    checks = fixed_store(args.root / 'baseline1/store')
    published = [x for x in rows(args.root / 'baseline1/save/manager/controller.jsonl') if x['type'] == 'session_published']
    assert len(published) == 1 and published[0]['sha256'] == sha(args.root / 'baseline1/store/session.fvs')
    assert all(x['passed'] for x in checks)
    build = read(args.root / 'build4/wait-build.json')
    source = Path(__file__).parent
    for item in build['sources']:
        assert sha(source / item['file']) == item['sha256'], item['file']
    for item in build['binaries']:
        assert sha(args.root / 'build4' / item['file']) == item['sha256'], item['file']
    before, after = [read(args.root / f'dependencies-{label}-pair.json') for label in ('before', 'after')]
    assert before['boot'] == after['boot'] and before['modules'] == after['modules']
    summary = dict(diagnosticSequenceCompleted=True, naturalWaitCases=len(cases), workerCOMSuccessorObservedInCases=sum(x['workerCOMSuccessorObserved'] for x in cases),
                   finalPair=read(args.root / 'final-pair/summary.json'), fixedStoreAudit=checks, buildAndSourceHashesMatch=True,
                   observedDependencyFilesUnchanged=True, cleanupCausalFixVerified=False, rootCauseConfirmed=False, fullR5004Passed=False, gateClosed=False,
                   serverOperationUnknown=True, serverThreadUnknown=True, globallyMinimalReproducerClaimed=False, nativeExperimentsExecutedByCollector=0)
    if args.regression:
        regression = read(args.regression)
        summary['pidFixCommit'] = '126eda4304f84c7c4b1d0a4f65b731dfc1edfc61'
        summary['restartBodyPassed'] = regression['normalExitCrossManagerRestorePassed']
        summary['completeOldRegressionsPassed'] = regression['allEvidenceChecksPassed']
        summary['restartCases'] = len(regression['cases'])
        summary['restartChecksIncludingRegressionAggregate'] = len(regression['checks'])
    (args.output / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    (args.output / 'wait-chains.json').write_text(json.dumps(cases, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
