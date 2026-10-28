option casemap:none
.code
PUBLIC CleanupSsp, CleanupRsp
CleanupSsp PROC
    rdsspq rax
    add rax,8
    ret
CleanupSsp ENDP
CleanupRsp PROC
    lea rax,[rsp+8]
    ret
CleanupRsp ENDP
END
