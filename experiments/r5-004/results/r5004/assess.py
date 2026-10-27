from pathlib import Path
import collections,hashlib,json
root=Path('D:/focus-views');base=root/'experiments/r5-004';raw=base/'results/g';regroot=base/'results/f/reg3';report=base/'results/r5004'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rows(p):return [json.loads(x) for x in p.read_text(encoding='utf-8-sig').splitlines() if x.strip()] if p.exists() else []
def main():
    summary=json.loads((raw/'summary.json').read_text(encoding='utf-8-sig'));report.mkdir(parents=True,exist_ok=True)
    events=collections.Counter();assertions=0;frames=0;pins=0;desktops=0;closes=0;forced=0;rescue=0;managers=[];native=[]
    for op in summary['operations']:
        m=Path(op['manager']);s=Path(op['supervisor']);owned=json.loads((s/'summary.json').read_text())
        record=rows(m/'controller.jsonl');supervisor=rows(s/'supervisor.jsonl')
        events.update(x['type'] for x in record);assertions+=sum(x['type']=='assertion' for x in record)
        frames+=len(list(m.rglob('*.png')));pins+=sum(x['type']=='job_instance_pinned' for x in supervisor);closes+=sum(x['type']=='owned_handle_closed' for x in supervisor);desktops+=sum(x['type']=='desktop_absent_confirmed' for x in supervisor)
        forced+=owned['forcedTermination'];rescue+=owned['explicitSurvivorCleanup']
        managers.append({**op,'certificateSha256':sha(Path(op['certificate'])) if Path(op['certificate']).exists() else None,'supervisorSummary':owned})
        native.extend(dict(x,managerPid=owned['managerPid']) for x in record if x['type'] in {'discard_window_native_result','unpublished_window_discarded'})
    regressions=[]
    for p in sorted(regroot.rglob('summary.json')):
        x=json.loads(p.read_text(encoding='utf-8-sig'))
        if 'allEvidenceChecksPassed' not in x:continue
        regressions.append({'file':p.relative_to(regroot.parent).as_posix(),'sha256':sha(p),'passed':x['allEvidenceChecksPassed'],'checks':len(x.get('checks',[])),'scenarios':len(x.get('cases',x.get('runs',[])))})
    stats={'scenarios':len(summary['cases']),'checks':len(summary['checks']),'failedChecks':sum(not x['passed'] for x in summary['checks']),'kindCounts':dict(collections.Counter(x['kind'] for x in summary['cases'])),'managerInstances':len(managers),'forcedManagerTerminations':forced,'explicitOwnedJobCleanup':rescue,'pinnedInstances':pins,'desktopsConfirmedAbsent':desktops,'ownedHandleCloses':closes,'rawPngFiles':frames,'nativeAssertions':assertions,'eventCounts':dict(sorted(events.items())),'allEvidenceChecksPassed':summary['allEvidenceChecksPassed'],'crashRecoveryVerified':summary['crashRecoveryVerified'],'regressions':json.loads((regroot/'summary.json').read_text(encoding='utf-8-sig'))['regressions'],'regressionEvidenceSource':'f/reg3/summary.json','latestMatrixRegressionStatus':summary['regressions'],'regressionsRerunInLatestMatrix':False,'hostRebootRecoveryVerified':False,'powerLossRecoveryVerified':False,'gateClosed':False,'manualHostTest':'not_executed'}
    for name,value in [('statistics.json',stats),('manager-identities.json',managers),('regressions.json',regressions),('rejected-window-cleanup.json',native)]:
        (report/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    regression=json.loads((regroot/'summary.json').read_text(encoding='utf-8-sig'))
    current_build=base.parent/'r5-003/out/build.json';saved_build=regroot/'build.json'
    def inputs_valid(build_path,relative_base):
        x=json.loads(build_path.read_text(encoding='utf-8-sig'))
        return all(sha((relative_base/e['file']).resolve())==e['sha256'] for e in x['sourceInputs'])
    identity=json.loads((raw/'identity.json').read_text(encoding='utf-8-sig'))
    checks={'latestCrashMatrixPassed':summary['allEvidenceChecksPassed'],'realCompleteOldRegressionPassed':regression['allEvidenceChecksPassed'] and regression['regressions']=='passed','oldRegressionBuildUnchanged':sha(current_build)==sha(saved_build),'oldRegressionInputsUnchanged':inputs_valid(current_build,base.parent/'r5-003'),'latestNativeInputsMatchBuild':inputs_valid(raw/'build.json',base),'archivedTestAndOracleMatchActualRun':sha(report/'original-test-g.ps1')==identity['testSha256'] and sha(base/'verification.ps1')==identity['verificationSha256'] and sha(base.parent/'r5-003/test.ps1')==identity['reusedOracleSha256']}
    assessment={'allRequiredEvidencePassed':all(checks.values()),'matrix':'g/summary.json','oldRegression':'f/reg3/summary.json','matrixOldRegressionRerun':False,'reason':'g failed when target image bases changed. Restore was refused and unconfirmed desktop cleanup blocked later attempts. f old regressions really passed with unchanged dependencies; neither that result nor the post-run diagnostic guard makes g recovery pass.','checks':checks,'firstFormalRun':'v/summary.json remains false: two desktop cleanups','secondFormalRun':'f/summary.json remains false: one image-query observation failure; nested regression passed','currentDriverMatchesG':sha(base/'test.ps1')==identity['testSha256'],'postRunReadOnlyGuardVerified':json.loads((base/'out/failure-guard.json').read_text(encoding='utf-8-sig'))['diagnosticGuardVerified'],'gateClosed':False,'hostRebootRecoveryVerified':False}
    (report/'assessment.json').write_text(json.dumps(assessment,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(stats,indent=2));print(json.dumps(assessment,indent=2))
if __name__=='__main__':main()
