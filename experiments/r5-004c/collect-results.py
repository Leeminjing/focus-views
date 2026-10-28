"""Read-only interpretation of native evidence; never changes a run summary."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(run, output, inline, diagnostic, probe, earlier, serial):
    overall = read(run / 'summary.json')
    body = {name: read(run / name / 'summary.json') for name in ('focus', 'cleanup', 'relocation', 'crash')}
    legacy = read(run / 'reg3/summary.json')
    assert len(overall['stages']) == 5, 'Mandatory suites must all finish, including independent cleanup and old regressions'
    project = Path(__file__).resolve().parent
    build = read(run / 'build.json')
    binary_root = project / 'out-final2'
    binary_audit = [dict(file=item['file'], expected=item['sha256'], actual=sha(binary_root / item['file'])) for item in build['binaries']]
    assert all(item['expected'] == item['actual'] for item in binary_audit), 'Frozen native build changed'
    source_audit = [dict(file=item['file'], expected=item['sha256'], actual=sha(project / item['file'])) for item in build['sourceInputs']]
    assert all(item['expected'] == item['actual'] for item in source_audit), 'Recorded final sources changed'
    assert not output.exists(), 'Preserve prior evidence'
    output.mkdir(parents=True)

    def copy(source, name):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    copy(run / 'summary.json', 'native-combination-summary.json')
    copy(run / 'build.json', 'build.json')
    for name in body:
        copy(run / name / 'summary.json', name + '-summary.json')
    copy(run / 'reg3/summary.json', 'legacy-summary.json')
    copy(earlier / 'summary.json', 'earlier-combination-summary.json')
    copy(diagnostic / 'diagnostic-assessment.json', 'synthetic-diagnostic-assessment.json')
    copy(diagnostic / 'summary.json', 'synthetic-original-failure-summary.json')
    copy(probe / 'summary.json', 'synchronization-probes-summary.json')
    copy(inline / 'summary.json', 'inline-counter-summary.json')
    (output / 'final-build-audit.json').write_text(json.dumps(dict(binaries=binary_audit, sources=source_audit, passed=True), indent=2) + '\n', encoding='utf-8')
    failures = []
    subjects = [(case['name'], run / 'relocation' / case['name']) for case in body['relocation']['cases'] if not case['passed']]
    subjects.append(('inline-counter', inline))
    for mode in ('inline', 'deferred'):
        subjects.append(('serial-' + mode, serial / ('serial-final-' + mode)))
    for label, root in subjects:
        manager = root / 'restore/manager'
        log = rows(manager / 'controller.jsonl')
        native = [r for r in log if r['type'] == 'abort_native_window_cleanup']
        if not native or native[0]['verified']:
            continue
        phases = [r for r in log if r['type'] == 'cleanup_phase']
        captured = manager / 'RB/cleanup-diagnostic.jsonl'
        sample = rows(captured) if captured.exists() else []
        first = next((r for r in phases if r['stage'] == 2), {})
        entry = next((r for r in phases if r['stage'] == 5), {})
        source = read(root / 'summary.json')
        checks = {r['name']: r['passed'] for r in source['checks']}
        failures.append(dict(
            case=label, verdict=native[0], phases=phases,
            entrySspEqualsCheckpoint=bool(first) and first['ssp'] == entry.get('ssp'),
            sspInterpretation='Equality is required for the resumed deferred entry; inline still has legitimate exception-handler call frames.',
            outsideVehObserved=all(stage in [r['stage'] for r in phases] for stage in (3, 4, 5)),
            noRBSourceWrites=not any(r['type'] == 'restore_source_write_begin' and r.get('view') == 'RB' for r in log),
            noRBPublication=not any(r['type'] == 'view_published' and r.get('view') == 'RB' for r in log),
            sourceAndArtifactsUnchanged=checks.get('fixed-store-unchanged-after-refusal', False),
            otherViewSurvived=checks.get('already-restored-RA-still-runs-and-paints', False),
            freshRecoveryCompleted=checks.get('restore-native-manager-completed', False),
            waitChain=[r for r in sample if r['type'] in ('wait_chain', 'wait_node')],
            frames=[r for r in sample if r['type'] == 'frame'],
            interpretation='Natural test invocation has no blocked: prefix; source enables synthetic window blocking only for that prefix. Export-only symbols may be nearest labels, not private function names.'
        ))
        copy(manager / 'controller.jsonl', 'natural/' + label + '/controller.jsonl')
        copy(root / 'summary.json', 'natural/' + label + '/summary.json')
        if captured.exists():
            copy(captured, 'natural/' + label + '/diagnostic.jsonl')
    (output / 'natural-counterexamples.json').write_text(json.dumps(failures, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    summary = dict(
        reportDate='2026-10-28', sourceBase='21e747eea101bfb95e33d83bb100569eda4b513c',
        candidateFunctionalVerificationPassed=overall['candidateFunctionalVerificationPassed'],
        focusPassed=body['focus']['passed'],
        independentCleanupMatrixPassed=body['cleanup']['allEvidenceChecksPassed'],
        relocationMatrixPassed=body['relocation']['passed'],
        originalCrashMatrixPassed=body['crash']['allEvidenceChecksPassed'],
        completeOldRegressionsPassed=legacy['allEvidenceChecksPassed'] and legacy['regressions'] == 'passed',
        sameBoot=overall['sameBoot'], sameFinalBuild=overall['sameFinalBuild'],
        naturalFailedCases=len(failures),
        deferredNaturalFailures=sum(r['outsideVehObserved'] for r in failures),
        inlineNaturalFailures=sum(not r['outsideVehObserved'] for r in failures),
        historicalBlockingPatternObserved=any(
            r['waitChain'] and any('UninitLocalMsCtfMonitor' in frame.get('symbol', '') for frame in r['frames'])
            for r in failures
        ),
        deferredCandidateSufficient=False,
        cleanupCausalFixVerified=False, fullR5004Passed=False, gateClosed=False,
        rootCauseConfirmed=False,
        candidateDisposition='experimental only; moving disposal out of VEH is insufficient, so do not replace the historical formal implementation as a proven fix',
        hostRebootRecoveryVerified=False, manualHostTest='not_executed',
        evidenceInterpretationIsNotANewNativeExperiment=True
    )
    (output / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--inline', required=True, type=Path)
    parser.add_argument('--diagnostic', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--earlier', required=True, type=Path)
    parser.add_argument('--serial', required=True, type=Path)
    args = parser.parse_args()
    collect(args.run, args.output, args.inline, args.diagnostic, args.probe, args.earlier, args.serial)
