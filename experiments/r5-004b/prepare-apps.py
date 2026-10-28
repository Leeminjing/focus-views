"""Copy the two verified applications and add only the opt-in image ABI.

Generated sources are committed for inspection; this is not a runtime source
transformer and never rewrites a deployed application's code or private memory.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parent
apps=root/'apps';apps.mkdir(exist_ok=True)
for name,original in [('fixture','../r5-001a/app/fixture.cpp'),('recorder','../r5-002/recorder/main.cpp')]:
    source=(root/original).read_text(encoding='utf-8')
    source=source.replace('#include "../scenario/fixture_contract.hpp"','#include "../../r5-001a/scenario/fixture_contract.hpp"')
    source=source.replace('#include "contract.hpp"','#include "../../r5-002/recorder/contract.hpp"')
    source=source.replace('#include "build_id.hpp"','#include "build_id.hpp"\n#include "../app-registration.hpp"\n')
    # Replace an implicit, compiler-spilled C++ reference by an addressable
    # volatile pointer. Its slot is explicitly declared while the frame is live.
    start=source.index('__declspec(noinline) void application()')
    end=source.index('\nvoid managed',start)
    body=source[start:end]
    metadata='R305Metadata' if name=='fixture' else 'R305Metadata.base'
    body=body.replace('auto& m='+metadata+';', 'auto* volatile m=&'+metadata+';')
    body=re.sub(r'\bm\.', 'm->', body)
    marker='m->ref_count=0;m->required_count=0;'
    assert marker in body
    body=body.replace(marker,'r5004b::begin_image_references();r5004b::image_slot(&m,r5004b::Location::stack,reinterpret_cast<std::uint64_t>(m));'+marker)
    source=source[:start]+body+source[end:]
    # A real, subsequently called image pointer in the leaf stack, a data slot,
    # and a register. Omitted reassociation cannot silently look successful.
    marker='    const auto reg='
    start=source.index(marker)
    declaration='''    auto* volatile callback=&CodeAnchor;
    const auto anchor_address=R305Metadata'''+('.base' if name=='recorder' else '')+'''.root+R305Metadata'''+('.base' if name=='recorder' else '')+'''.data_bytes-8;
    auto* const anchor=reinterpret_cast<volatile std::uint64_t*>(anchor_address);
    *anchor=reinterpret_cast<std::uint64_t>(&CodeAnchor);
    r5004b::image_slot(&callback,r5004b::Location::stack,*anchor);
    r5004b::image_slot(anchor,r5004b::Location::data,*anchor);
    r5004b::image_slot(nullptr,r5004b::Location::r8,*anchor);
'''
    source=source[:start]+declaration+source[start:]
    start=source.index(marker);end=source.index('\n',start)+1
    source=source[:end]+'''    if(reinterpret_cast<std::uint64_t>(callback)!=*anchor||callback()!=0x5004B)reject(ERROR_INVALID_ADDRESS);
'''+source[end:]
    (apps/(name+'.cpp')).write_text('// R5-004b opt-in derivative; see prepare-apps.py for the exact ABI-only changes.\n'+source,encoding='utf-8',newline='\n')
asm=(root/'../r5-002/recorder/traps.asm').read_text(encoding='utf-8')
asm=asm.replace('CheckpointTrap PROC\n','CheckpointTrap PROC\n    lea r8,CodeAnchor\n')
asm=asm.replace('    mov rax,r10\n    ret', '    lea r9,CodeAnchor\n    cmp r8,r9\n    je register_valid\n    ud2\nregister_valid:\n    mov rax,r10\n    ret')
asm=asm.replace('END\n','PUBLIC CodeAnchor\nCodeAnchor PROC\n    mov rax,5004Bh\n    ret\nCodeAnchor ENDP\nEND\n')
(apps/'traps.asm').write_text(asm,encoding='utf-8',newline='\n')
