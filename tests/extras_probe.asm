.code
ProbePickupDelay PROC FRAME
    push rsi
    .pushreg rsi
    push r15
    .pushreg r15
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    mov r15, rcx
    mov rsi, rdx
    call r8
    add rsp, 58h
    pop r15
    pop rsi
    ret
ProbePickupDelay ENDP
END
