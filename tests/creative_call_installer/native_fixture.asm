; Isolated harness, never linked into gameplay artifacts.
EXTERN creative_g_dispatch_bridge:PROC
EXTERN creative_g_fixture_prepare:PROC
EXTERN creative_g_fixture_complete:PROC
PUBLIC creative_g_installer_fixture
PUBLIC creative_g_installer_site
.code
SNAPSHOT MACRO delta
    push r10
    mov r10, [rsp+3A8h]
    lea r10, [r10+delta]
    mov [r10+00h], rax
    mov [r10+08h], rcx
    mov [r10+10h], rdx
    mov [r10+18h], rbx
    push rax
    lea rax, [rsp+10h]
    mov [r10+20h], rax
    mov rax, [rsp+8]
    mov [r10+50h], rax
    pop rax
    mov [r10+28h], rbp
    mov [r10+30h], rsi
    mov [r10+38h], rdi
    mov [r10+40h], r8
    mov [r10+48h], r9
    mov [r10+58h], r11
    mov [r10+60h], r12
    mov [r10+68h], r13
    mov [r10+70h], r14
    mov [r10+78h], r15
    movdqu [r10+80h], xmm0
    movdqu [r10+90h], xmm1
    movdqu [r10+0a0h], xmm2
    movdqu [r10+0b0h], xmm3
    movdqu [r10+0c0h], xmm4
    movdqu [r10+0d0h], xmm5
    movdqu [r10+0e0h], xmm6
    movdqu [r10+0f0h], xmm7
    movdqu [r10+100h], xmm8
    movdqu [r10+110h], xmm9
    movdqu [r10+120h], xmm10
    movdqu [r10+130h], xmm11
    movdqu [r10+140h], xmm12
    movdqu [r10+150h], xmm13
    movdqu [r10+160h], xmm14
    movdqu [r10+170h], xmm15
    stmxcsr [r10+180h]
    pushfq
    pop qword ptr [r10+188h]
    pop r10
ENDM
creative_g_installer_fixture PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp,408h
    .allocstack 408h
    movaps [rsp+300h], xmm6
    .savexmm128 xmm6, 300h
    movaps [rsp+310h], xmm7
    .savexmm128 xmm7, 310h
    movaps [rsp+320h], xmm8
    .savexmm128 xmm8, 320h
    movaps [rsp+330h], xmm9
    .savexmm128 xmm9, 330h
    movaps [rsp+340h], xmm10
    .savexmm128 xmm10, 340h
    movaps [rsp+350h], xmm11
    .savexmm128 xmm11, 350h
    movaps [rsp+360h], xmm12
    .savexmm128 xmm12, 360h
    movaps [rsp+370h], xmm13
    .savexmm128 xmm13, 370h
    movaps [rsp+380h], xmm14
    .savexmm128 xmm14, 380h
    movaps [rsp+390h], xmm15
    .savexmm128 xmm15, 390h
    lea rbp, [rsp+0F0h]
    .setframe rbp, 0F0h
    .endprolog
    mov [rsp+3A0h], rdx
    stmxcsr [rsp+3B0h]
    mov rsi, rcx
    lea rdi, [rbp-60h]
    mov ecx,27h
    rep movsq
    mov qword ptr [rbp+1B8h], 0ABCh
    lea rcx, [rbp-60h]
    lea rdx, [rbp+1B8h]
    call creative_g_fixture_prepare
    pcmpeqd xmm0, xmm0
    pslld xmm0, 0
    pcmpeqd xmm1, xmm1
    pslld xmm1, 1
    pcmpeqd xmm2, xmm2
    pslld xmm2, 2
    pcmpeqd xmm3, xmm3
    pslld xmm3, 3
    pcmpeqd xmm4, xmm4
    pslld xmm4, 4
    pcmpeqd xmm5, xmm5
    pslld xmm5, 5
    pcmpeqd xmm6, xmm6
    pslld xmm6, 6
    pcmpeqd xmm7, xmm7
    pslld xmm7, 7
    pcmpeqd xmm8, xmm8
    pslld xmm8, 8
    pcmpeqd xmm9, xmm9
    pslld xmm9, 9
    pcmpeqd xmm10, xmm10
    pslld xmm10, 10
    pcmpeqd xmm11, xmm11
    pslld xmm11, 11
    pcmpeqd xmm12, xmm12
    pslld xmm12, 12
    pcmpeqd xmm13, xmm13
    pslld xmm13, 13
    pcmpeqd xmm14, xmm14
    pslld xmm14, 14
    pcmpeqd xmm15, xmm15
    pslld xmm15, 15
    mov rax,11111111h
    mov rcx,22222222h
    mov rdx,33333333h
    mov rbx,44444444h
    mov rsi,55555555h
    mov rdi,66666666h
    mov r8,77777777h
    mov r9,88888888h
    mov r10,99999999h
    mov r11,0AAAAAAAAh
    mov r12,0BBBBBBBBh
    mov r13,0CCCCCCCCh
    mov r14,0DDDDDDDDh
    mov r15,0EEEEEEEEh
    push 246h
    popfq
    SNAPSHOT 0
    ; Exactly the future 8-byte site patch: FF15 rel32 / NOP / NOP.
    ALIGN 16
    db 090h,090h,090h,090h,090h
creative_g_installer_site LABEL BYTE
    db 048h,08Bh,045h,038h,00Fh,0B6h,048h,03Dh
    SNAPSHOT 190h
    lea rcx, [rbp-60h]
    call creative_g_fixture_complete
    ldmxcsr [rsp+3B0h]
    movaps xmm6, [rsp+300h]
    movaps xmm7, [rsp+310h]
    movaps xmm8, [rsp+320h]
    movaps xmm9, [rsp+330h]
    movaps xmm10, [rsp+340h]
    movaps xmm11, [rsp+350h]
    movaps xmm12, [rsp+360h]
    movaps xmm13, [rsp+370h]
    movaps xmm14, [rsp+380h]
    movaps xmm15, [rsp+390h]
    lea rsp, [rbp+318h]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
creative_g_installer_fixture ENDP
END
