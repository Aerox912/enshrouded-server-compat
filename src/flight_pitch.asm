EXTERN FlightPitchContinue:QWORD
EXTERN FlightVanillaPitch:DWORD
EXTERN FlightEnabledPitch:DWORD
EXTERN FlightAuthorize:PROC
.code
FlightPitchDetour PROC
    ; A mid-function jump arrives with RSP aligned to 16 bytes. Preserve the
    ; incoming condition codes used by JA immediately after the replaced MOVSS.
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 90h
    movdqu XMMWORD PTR [rsp+20h], xmm0
    movdqu XMMWORD PTR [rsp+30h], xmm1
    movdqu XMMWORD PTR [rsp+40h], xmm2
    movdqu XMMWORD PTR [rsp+50h], xmm3
    movdqu XMMWORD PTR [rsp+60h], xmm4
    movdqu XMMWORD PTR [rsp+70h], xmm5
    stmxcsr DWORD PTR [rsp+80h]
    mov rcx, QWORD PTR [rbp+13e0h]
    mov edx, DWORD PTR [rbp+3b0h]
    ; Arguments 3/4 report the incoming desired pitch and vertical velocity.
    ; XMM2/XMM3 are saved above and restored below, just like every volatile.
    movaps xmm2, xmm9
    movaps xmm3, xmm1
    shufps xmm3, xmm3, 55h
    call FlightAuthorize
    test al, al
    jz denied
    movss xmm0, DWORD PTR [FlightEnabledPitch]
    jmp restore
denied:
    movss xmm0, DWORD PTR [FlightVanillaPitch]
restore:
    ; MOVSS from memory clears the upper 96 bits, matching the original opcode.
    movdqu xmm1, XMMWORD PTR [rsp+30h]
    movdqu xmm2, XMMWORD PTR [rsp+40h]
    movdqu xmm3, XMMWORD PTR [rsp+50h]
    movdqu xmm4, XMMWORD PTR [rsp+60h]
    movdqu xmm5, XMMWORD PTR [rsp+70h]
    ldmxcsr DWORD PTR [rsp+80h]
    add rsp, 90h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [FlightPitchContinue]
FlightPitchDetour ENDP
END
