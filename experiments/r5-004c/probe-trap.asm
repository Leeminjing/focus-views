option casemap:none
.code
PUBLIC ProbeTrap, ProbeSite, ProbeSsp
ProbeTrap PROC
    rdsspq r11
ProbeSite LABEL BYTE
    int 3
    ret
ProbeTrap ENDP
ProbeSsp PROC
    rdsspq rax
    ret
ProbeSsp ENDP
END
