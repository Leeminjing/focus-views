option casemap:none
.code
PUBLIC FixtureReady, WorkloadStart, WorkloadCheckpoint, WorkloadService, WorkloadResult, WorkloadEnd
FixtureReady PROC
    int 3
    ud2
FixtureReady ENDP

WorkloadStart PROC
    int 3
    mov r12, rdx
    mov r13, r8
    mov rax, rcx
    rol rax, 17
    mov r11, 0A17C9E3D52B468F1h
    xor rax, r11
    mov [r12], rax
    mov [r12+8], r12
    mov [r12+16], r12
    sub rsp, 80h
    mov rax, 06E24B59A0173D8CFh
    xor rax, rcx
    mov [rsp+20h], rax
    mov r15, rcx
    rol r15, 31
    mov r11, 013579BDF2468ACE1h
    add r15, r11
    cmp r13, 0
    je BeforeCheckpoint
    ; Source only: ordinary open -> duplicate -> seek, plus a second independent
    ; open of the SAME catalog file. All actual operations go through the runtime.
    mov qword ptr [r12+256], 10
    lea r14, AfterOpen1
    jmp NEAR PTR WorkloadService
AfterOpen1:
    mov rax, [r12+304]
    mov [r12+144], rax
    mov [r12+264], rax
    mov qword ptr [r12+256], 14
    lea r14, AfterDuplicate
    jmp NEAR PTR WorkloadService
AfterDuplicate:
    mov rax, [r12+304]
    mov [r12+152], rax
    mov qword ptr [r12+256], 13
    mov qword ptr [r12+272], 2
    lea r14, AfterSeek1
    jmp NEAR PTR WorkloadService
AfterSeek1:
    mov qword ptr [r12+256], 10
    mov qword ptr [r12+272], 0
    lea r14, AfterOpen2
    jmp NEAR PTR WorkloadService
AfterOpen2:
    mov rax, [r12+304]
    mov [r12+160], rax
    mov [r12+264], rax
    mov qword ptr [r12+256], 13
    mov qword ptr [r12+272], 6
    lea r14, BeforeCheckpoint
    jmp NEAR PTR WorkloadService
BeforeCheckpoint:
    stc
WorkloadCheckpoint LABEL BYTE
    int 3
    mov rax, [r12]
    adc rax, r15
    xor rax, [rsp+20h]
    rol rax, 9
    mov [r12+32], rax
    mov rax, [r12]
    mov [r12+96], rax
    mov rax, [rsp+20h]
    mov [r12+104], rax
    mov [r12+112], r15
WorkloadResult LABEL BYTE
    int 3
    cmp qword ptr [r12+40], 1
    jne FileCommand
    mov r10, [r12+8]
    mov rax, [r12+48]
    add [r10], rax
    mov r10, [r12+16]
    mov rax, [r10]
    mov [r12+96], rax
    inc qword ptr [r12+24]
    xor rax, r15
    xor rax, [rsp+20h]
    mov [r12+32], rax
    jmp NEAR PTR WorkloadResult
FileCommand:
    mov rax, [r12+40]
    mov [r12+256], rax
    mov rax, [r12+56]
    mov [r12+264], rax
    mov rax, [r12+48]
    mov [r12+272], rax
    mov rax, [r12+64]
    mov [r12+280], rax
    mov rax, [r12+72]
    mov [r12+288], rax
    mov rax, [r12+80]
    mov [r12+296], rax
    lea r14, AfterRequest
    jmp NEAR PTR WorkloadService
AfterRequest:
    mov rax, [r12+304]
    mov [r12+88], rax
    xor rax, [r12]
    xor rax, r15
    xor rax, [rsp+20h]
    mov [r12+32], rax
    mov rax, [r12+312]
    mov [r12+136], rax
    mov rax, [r12+288]
    mov [r12+120], rax
    mov rax, [r12+296]
    mov [r12+128], rax
    inc qword ptr [r12+24]
    jmp NEAR PTR WorkloadResult
WorkloadService LABEL BYTE
    int 3
    jmp r14
WorkloadEnd LABEL BYTE
WorkloadStart ENDP
END
