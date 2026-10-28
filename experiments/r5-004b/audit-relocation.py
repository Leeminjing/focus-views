"""Read-only, byte-for-byte audit from declared slots and saved raw state."""
import hashlib,json,struct,sys
from pathlib import Path

def rows(path):return [json.loads(x) for x in path.read_text(encoding='utf-8-sig').splitlines() if x]
def u64(data,offset):return struct.unpack_from('<Q',data,offset)[0]
def sha(data):return hashlib.sha256(data).hexdigest()
def audit(first,second):
    source=rows(first/'controller.jsonl');target=rows(second/'controller.jsonl')
    checks=[]
    def check(name,okay):
        checks.append(dict(name=name,passed=bool(okay)))
        if not okay:raise ValueError(name)
    for label,view in [('S0','C'),('SA','RA'),('SB','RB')]:
        prefix=f'{label}.{view}'
        plan=next(r for r in target if r['type']=='image_reassociation_prepared' and r['view']==view)
        snapshot=next(r for r in source if r['type']=='snapshot' and r['label']==label)
        context_layout=next(r for r in target if r['type']=='image_context_layout' and r['view']==view)
        raw_layout=next(r for r in source if r['type']=='raw_layout')
        root=raw_layout['dataAddress'];rsp=snapshot['rsp'];a=plan['sourceBase'];b=plan['targetBase'];delta=b-a
        check(prefix+'.real-base-change',a!=b)
        memory=bytearray((first/f'{label}-data.bin').read_bytes());stack=bytearray((first/f'{label}-active-stack.bin').read_bytes())
        cpu=(first/f'{label}-context.bin').read_bytes()
        registry=(first/f'{label}-image-references.bin').read_bytes()
        slot_bytes=(first/f'{label}-image-return-slots.bin').read_bytes()
        check(prefix+'.loaded-declarations-identical',registry==(second/f'{label}-image-references.bin').read_bytes() and slot_bytes==(second/f'{label}-image-return-slots.bin').read_bytes())
        magic,version,size,count,required=struct.unpack_from('<QIIII',registry)
        check(prefix+'.profile-version-and-bounds',magic==0x3152464942564646 and version==2 and size==len(registry)==408 and count==required==4)
        returns=list(struct.unpack('<'+'Q'*(len(slot_bytes)//8),slot_bytes))
        saved_returns=struct.unpack('<'+'Q'*len(returns),(first/f'{label}-returns.bin').read_bytes())
        events=[r for r in target if r['type']=='image_reassociation' and r['view']==view]
        def patch(slot,before,after):
            if root<=slot<=root+len(memory)-8:buffer,offset=memory,slot-root
            elif rsp<=slot<=rsp+len(stack)-8:buffer,offset=stack,slot-rsp
            else:raise ValueError(prefix+'.undeclared-slot-bounds')
            check(prefix+f'.preimage-{slot}',u64(buffer,offset)==before)
            struct.pack_into('<Q',buffer,offset,after)
        for slot,value in zip(returns,saved_returns):
            check(prefix+f'.logged-return-{slot}',len([r for r in events if r['kind']=='unwind-return' and r['slot']==slot and r['before']==value and r['after']==value+delta])==1)
            patch(slot,value,value+delta)
        r8=u64(cpu,context_layout['r8Offset'])
        for index in range(count):
            kind,reserved,slot,rva=struct.unpack_from('<IIQQ',registry,24+index*24)
            check(prefix+f'.declared-kind-{index}',reserved==0 and kind in (1,2,4))
            if kind==4:check(prefix+'.registered-R8-preimage',slot==0 and r8==a+rva);r8=b+rva
            else:patch(slot,a+rva,b+rva)
            check(prefix+f'.logged-reference-{index}',len([r for r in events if r['kind']=='registered-image-reference' and r['slot']==slot and r['before']==a+rva and r['after']==b+rva])==1)
        rebound=[r for r in target if r['type']=='resource_rebind' and r['view']==view]
        check(prefix+'.seven-native-HWND-references',len(rebound)==7)
        for r in rebound:patch(r['address'],r['oldHwnd'],r['newHwnd'])
        actual_memory=(second/view/'target-data-before-continue.bin').read_bytes()
        actual_stack=(second/view/'target-active-stack-before-continue.bin').read_bytes()
        check(prefix+'.all-data-bytes-exactly-declared-changes',memory==actual_memory)
        check(prefix+'.all-active-stack-bytes-exactly-declared-changes',stack==actual_stack)
        got=(second/view/'target-context-before-continue.bin').read_bytes()
        for key,expected in [('ripOffset',u64(cpu,context_layout['ripOffset'])+delta),('rspOffset',rsp),('r10Offset',u64(cpu,context_layout['r10Offset'])),('r8Offset',r8)]:
            check(prefix+'.CPU-'+key,u64(got,context_layout[key])==expected)
        match=next(r for r in target if r['type']=='chain_match' and r['view']==view)
        check(prefix+'.target-SSP-preserved',u64(got,context_layout['r11Offset'])==match['targetSsp'])
        checks.append(dict(name=prefix+'.prepared-byte-hashes',passed=True,dataSha256=sha(memory),activeStackSha256=sha(stack)))
    return checks

if __name__=='__main__':
    first,second,output=map(Path,sys.argv[1:]);checks=[];error=''
    try:checks=audit(first,second)
    except Exception as e:error=f'{type(e).__name__}: {e}'
    result=dict(passed=not error,error=error,checks=checks,readOnly=True)
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=result['passed'],checks=len(checks),error=error)))
    sys.exit(0 if result['passed'] else 1)
