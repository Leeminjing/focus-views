option casemap:none
EXTERN R305ManagedEntry:PROC
EXTERN R305Rebuild:DWORD
.code
PUBLIC EnterManagedStack, ReadyTrap, ReadySite, InitTrap, CheckpointTrap, CheckpointSite, ContinuationTrap, RejectTrap
PUBLIC AlternateCheckpointTrap, AlternateCheckpointSite, OuterWorkTrap, MiddleWorkTrap, LeafWorkTrap, ChangedTrap
; One bootstrap pivot only. Thereafter the normal C++ ABI, CALL/RET, /GS and
; real Win32 functions execute on this runtime-owned stack. Never return to CRT.
EnterManagedStack PROC
    mov rsp,rcx
    and rsp,-16
    sub rsp,20h
    call R305ManagedEntry
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
    cmp dword ptr [R305Rebuild],0
    jne structural_entry
    rdtsc
    shl rdx,32
    or rax,rdx
    mov r10,rax
    jmp checkpoint_common
structural_entry:
    mov r10,0CAFE1173DEAD9911h
checkpoint_common:
    xor eax,eax
    xor edx,edx
    rdsspq r11
CheckpointSite LABEL BYTE
    int 3
    mov rax,r10
    ret
CheckpointTrap ENDP
AlternateCheckpointTrap PROC
    mov r10,0CAFE1173DEAD9911h
    rdsspq r11
AlternateCheckpointSite LABEL BYTE
    int 3
    mov rax,r10
    ret
AlternateCheckpointTrap ENDP
OuterWorkTrap PROC
    int 3
    ret
OuterWorkTrap ENDP
MiddleWorkTrap PROC
    int 3
    ret
MiddleWorkTrap ENDP
LeafWorkTrap PROC
    int 3
    ret
LeafWorkTrap ENDP
ChangedTrap PROC
    int 3
    ret
ChangedTrap ENDP
ContinuationTrap PROC
    int 3
    ret
ContinuationTrap ENDP
RejectTrap PROC
    int 3
    ud2
RejectTrap ENDP
FourthWorkTrap PROC
    int 3
    ret
FourthWorkTrap ENDP
END
