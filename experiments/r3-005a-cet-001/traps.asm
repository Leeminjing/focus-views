option casemap:none
.code
PUBLIC EnterManagedStack, ReadyTrap, ReadySite, InitTrap, CheckpointTrap, CheckpointSite, ContinuationTrap, RejectTrap
PUBLIC ApiTrap, ApiSite, DiagnosticTrap, DiagnosticSite
; One bootstrap pivot only. Thereafter the normal C++ ABI, CALL/RET, /GS and
; real Win32 functions execute on this runtime-owned stack. Never return to CRT.
EnterManagedStack PROC
    mov rax,rcx
    mov rsp,rdx
    and rsp,-16
    sub rsp,20h
    call rax
    ud2
EnterManagedStack ENDP
ReadyTrap PROC
    rdsspq r11
ReadySite LABEL BYTE
    int 3
    ret
ReadyTrap ENDP
InitTrap PROC
    int 3
    ret
InitTrap ENDP
CheckpointTrap PROC
    rdtsc
    shl rdx,32
    or rax,rdx
    mov r10,rax
    xor eax,eax
    xor edx,edx
    rdsspq r11
CheckpointSite LABEL BYTE
    int 3
    mov rax,r10
    ret
CheckpointTrap ENDP
ContinuationTrap PROC
    int 3
    ret
ContinuationTrap ENDP
RejectTrap PROC
    int 3
    ud2
RejectTrap ENDP
ApiTrap PROC
    rdsspq r11
ApiSite LABEL BYTE
    int 3
    ret
ApiTrap ENDP
DiagnosticTrap PROC
    rdsspq r11
DiagnosticSite LABEL BYTE
    int 3
    ret
DiagnosticTrap ENDP
END
