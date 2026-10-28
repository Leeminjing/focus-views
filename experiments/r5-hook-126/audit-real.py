"""Read-only verification of diagnostics from real legacy application paths."""
import argparse, hashlib, json
from pathlib import Path

def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def run(root, output):
    root, output = Path(root), Path(output)
    if output.exists():
        raise RuntimeError('Preserve earlier audit')
    channels, errors, assertions = [], [], []
    required = {'LoadLibraryW.runtime', 'GetProcAddress.ManagedGetMessage',
                'SetWindowsHookExW.thread_only', 'GetProcAddress.ManagedCallWindow',
                'SetWindowsHookExW.thread_control', 'UnhookWindowsHookEx.control',
                'UnhookWindowsHookEx.queue', 'FreeLibrary.bridge_module'}
    for path in sorted(root.rglob('*-input.jsonl')):
        rows = [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line]
        diagnostics = [r for r in rows if r.get('type') == 'input_install_diagnostic']
        if not diagnostics:
            continue  # Other runtime generations are not the instrumented path.
        events = {}
        for row in diagnostics:
            events.setdefault(row['event'], []).append(row)
        api = events.get('api', [])
        identity = events.get('target_identity', [])
        helper = events.get('helper_identity', [])
        loaded = events.get('loaded_module', [])
        owners = events.get('export_owner', [])
        checks = {
            'one_argument': len(events.get('dll_argument', [])) == 1,
            'target_instance': len(identity) == 1 and identity[0]['targetPid'] == identity[0]['threadOwner']
                and identity[0]['created'] > 0 and identity[0]['threadCreated'] > 0
                and identity[0]['processWait'] == 258 and identity[0]['threadWait'] == 258,
            'same_desktop': len(helper) == 1 and len(identity) == 1
                and helper[0]['desktop'] == identity[0]['desktop'],
            'module_and_exports': len(loaded) == 1 and len(owners) == 2
                and all(r['sameModule'] and r['passedModule'] == loaded[0]['module'] for r in owners),
            'raw_api_sequence': [r['api'] for r in api] == [
                'LoadLibraryW.runtime', 'GetProcAddress.ManagedGetMessage', 'SetWindowsHookExW.thread_only',
                'GetProcAddress.ManagedCallWindow', 'SetWindowsHookExW.thread_control',
                'UnhookWindowsHookEx.control', 'UnhookWindowsHookEx.queue', 'FreeLibrary.bridge_module'],
            'raw_calls_succeeded_this_run': {r['api'] for r in api} == required
                and all(r['error'] == 0 and r['result'] != 0 for r in api),
            'no_diagnostic_query_failures': not events.get('diagnostic_failure'),
        }
        for kind in ['helper_identity', 'argument_file', 'loaded_module']:
            records = events.get(kind, [])
            checks[kind + '_disk_identity'] = len(records) == 1 and hashlib.sha256(
                Path(records[0]['path']).read_bytes()).hexdigest() == records[0]['diskSha256']
        for name, passed in checks.items():
            assertions.append(dict(file=str(path.relative_to(root)), check=name, passed=passed))
        errors.extend(dict(file=str(path.relative_to(root)), **r) for r in api if r['error'])
        channels.append(dict(file=str(path.relative_to(root)), target=identity, helper=helper,
                             loaded=loaded, exports=owners, api=api, checks=checks))
    passed = bool(channels) and all(a['passed'] for a in assertions)
    result = dict(passed=passed, readOnly=True, newNativeExperiment=False,
                  channels=channels, checks=assertions, apiErrors=errors,
                  hook126Observed=any(r['api'].startswith('SetWindowsHookExW') and r['error'] == 126 for r in errors),
                  rootCauseConfirmed=False, cleanupCausalFixVerified=False, fullR5004Passed=False)
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(dict(passed=passed, channels=len(channels), checks=len(assertions), hook126Observed=result['hook126Observed'])))
    if not passed:
        raise RuntimeError('Real path diagnostics incomplete or failed; retain raw records')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root'); parser.add_argument('output')
    args = parser.parse_args(); run(args.root, args.output)
