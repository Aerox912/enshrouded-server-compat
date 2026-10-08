.code
ProbeFlight PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    sub rsp, 1498h
    .allocstack 1498h
    .endprolog
    ; Reproduce only the pinned stack slots read by the glide detour.
    lea rbp, [rsp+80h]
    mov QWORD PTR [rbp+13e0h], rcx
    mov DWORD PTR [rbp+3b0h], edx
    mov QWORD PTR [rsp+20h], r9
    mov QWORD PTR [rsp+28h], r8
    mov rbx, QWORD PTR [rsp+14d0h]
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pcmpeqd xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pcmpeqd xmm5, xmm5
    mov rax, 111h
    mov rcx, 222h
    mov rdx, 333h
    mov r8, 444h
    mov r9, 555h
    mov r10, 666h
    mov r11, 777h
    push QWORD PTR [rsp+28h]
    popfq
    jmp rbx
ProbeFlight ENDP

ProbeFlightTail PROC
    mov rbx, QWORD PTR [rsp+20h]
    mov QWORD PTR [rbx], rax
    mov QWORD PTR [rbx+8h], rcx
    mov QWORD PTR [rbx+10h], rdx
    mov QWORD PTR [rbx+18h], r8
    mov QWORD PTR [rbx+20h], r9
    mov QWORD PTR [rbx+28h], r10
    mov QWORD PTR [rbx+30h], r11
    pushfq
    pop rax
    mov QWORD PTR [rbx+38h], rax
    movdqu XMMWORD PTR [rbx+40h], xmm0
    movdqu XMMWORD PTR [rbx+50h], xmm1
    movdqu XMMWORD PTR [rbx+60h], xmm2
    movdqu XMMWORD PTR [rbx+70h], xmm3
    movdqu XMMWORD PTR [rbx+80h], xmm4
    movdqu XMMWORD PTR [rbx+90h], xmm5
    stmxcsr DWORD PTR [rbx+0a0h]
    add rsp, 1498h
    pop rbx
    pop rbp
    ret
ProbeFlightTail ENDP

ClobberFlightVolatiles PROC
    mov rax, -1
    mov rcx, -1
    mov rdx, -1
    mov r8, -1
    mov r9, -1
    mov r10, -1
    mov r11, -1
    xorps xmm0, xmm0
    xorps xmm1, xmm1
    xorps xmm2, xmm2
    xorps xmm3, xmm3
    xorps xmm4, xmm4
    xorps xmm5, xmm5
    test rax, rax
    ret
ClobberFlightVolatiles ENDP
END
