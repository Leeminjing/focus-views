; Position-independent, single-threaded x64 fragment. No calls, returns, imports,
; TLS, syscalls, or references to the fixture's ordinary heap / startup stack.
option casemap:none
.code
PUBLIC FixtureReady, WorkloadStart, WorkloadCheckpoint, WorkloadResult, WorkloadEnd

FixtureReady PROC
    int 3
    ud2                         ; a debugger must redirect at the ready trap
FixtureReady ENDP

WorkloadStart PROC
    int 3                       ; independently counted task initialization
    mov r12, rdx                ; managed data base
    mov rax, rcx                ; unpredictable seed supplied only to source
    rol rax, 17
    mov r11, 0A17C9E3D52B468F1h
    xor rax, r11
    mov [r12], rax              ; unsaved private memory contribution

    sub rsp, 80h
    mov rax, 06E24B59A0173D8CFh
    xor rax, rcx
    mov [rsp+20h], rax          ; stack-local contribution

    mov r15, rcx
    rol r15, 31
    mov r11, 013579BDF2468ACE1h
    add r15, r11                ; intermediate exists only in R15
    stc                         ; also prove carry flag restoration
WorkloadCheckpoint LABEL BYTE
    int 3
    ; CONTEXT.Rip resumes here (one byte after the checkpoint INT3).
    mov rax, [r12]
    adc rax, r15
    xor rax, [rsp+20h]
    rol rax, 9
    mov [r12+8], rax
    mov [r12+16], r15           ; diagnostic observations only AFTER resume
    mov rdx, [rsp+20h]
    mov [r12+24], rdx
WorkloadResult LABEL BYTE
    int 3
    jmp $                      ; controller terminates at result, never RETs
WorkloadEnd LABEL BYTE
WorkloadStart ENDP
END
