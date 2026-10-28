"""Counterexamples for the read-only audit; original native evidence is pinned."""
import importlib.util,json,shutil,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('audit-relocation.py'))
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
first,second,out=map(Path,sys.argv[1:]);out.mkdir(exist_ok=False)
needed=['controller.jsonl']
for label in ('S0','SA','SB'):needed.extend((f'{label}-image-references.bin',f'{label}-image-return-slots.bin'))
for view in ('RA','RB','C'):needed.extend(f'{view}/{suffix}' for suffix in ('target-data-before-continue.bin','target-active-stack-before-continue.bin','target-context-before-continue.bin'))
pinned={name:audit.sha((second/name).read_bytes()) for name in needed};checks=[]
for name,relative,offset,reason in [
 ('unregistered-data-change','RA/target-data-before-continue.bin',8,'all-data-bytes-exactly-declared-changes'),
 ('unregistered-stack-change','RA/target-active-stack-before-continue.bin',0,'all-active-stack-bytes-exactly-declared-changes'),
 ('wrong-RIP','RA/target-context-before-continue.bin',248,'CPU-ripOffset'),
 ('wrong-R8','RA/target-context-before-continue.bin',184,'CPU-r8Offset'),
 ('wrong-target-SSP','RA/target-context-before-continue.bin',208,'target-SSP-preserved')]:
    case=out/name
    for path in needed:
        target=case/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(second/path,target)
    path=case/relative;data=bytearray(path.read_bytes());data[offset]^=1;path.write_bytes(data)
    error=''
    try:audit.audit(first,case)
    except ValueError as e:error=str(e)
    checks.append(dict(case=name,passed=error=='SA.RA.'+reason,error=error,nativeExperiment=False))
checks.append(dict(case='original-native-evidence-unchanged',passed=all(audit.sha((second/name).read_bytes())==digest for name,digest in pinned.items())))
passed=all(r['passed'] for r in checks)
(out/'summary.json').write_text(json.dumps(dict(passed=passed,checks=checks,nativeExperiments=0),indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(passed=passed,checks=len(checks))))
sys.exit(0 if passed else 1)
