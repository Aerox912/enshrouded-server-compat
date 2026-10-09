; G-DISPATCH6: entered by a real FF15 CALL at the verified native MOV pair.
; No hook installation. All native state except the two displaced MOV outputs
; is restored; /EHa is required only in the C++ runtime route.
EXTERN creative_g_dispatch_route:PROC
PUBLIC creative_g_dispatch_bridge
.code
creative_g_dispatch_bridge PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    sub rsp, 1A8h
    .allocstack 1A8h
    mov [rsp+38h], rbx
    .savereg rbx, 38h
    mov [rsp+40h], rsi
    .savereg rsi, 40h
    mov [rsp+48h], rdi
    .savereg rdi, 48h
    mov [rsp+70h], r12
    .savereg r12, 70h
    mov [rsp+78h], r13
    .savereg r13, 78h
    mov [rsp+80h], r14
    .savereg r14, 80h
    mov [rsp+88h], r15
    .savereg r15, 88h
    movaps [rsp+100h], xmm6
    .savexmm128 xmm6, 100h
    movaps [rsp+110h], xmm7
    .savexmm128 xmm7, 110h
    movaps [rsp+120h], xmm8
    .savexmm128 xmm8, 120h
    movaps [rsp+130h], xmm9
    .savexmm128 xmm9, 130h
    movaps [rsp+140h], xmm10
    .savexmm128 xmm10, 140h
    movaps [rsp+150h], xmm11
    .savexmm128 xmm11, 150h
    movaps [rsp+160h], xmm12
    .savexmm128 xmm12, 160h
    movaps [rsp+170h], xmm13
    .savexmm128 xmm13, 170h
    movaps [rsp+180h], xmm14
    .savexmm128 xmm14, 180h
    movaps [rsp+190h], xmm15
    .savexmm128 xmm15, 190h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rbp+20h], rax
    mov [rbp+28h], rcx
    mov [rbp+30h], rdx
    mov [rbp+50h], r8
    mov [rbp+58h], r9
    mov [rbp+60h], r10
    mov [rbp+68h], r11
    stmxcsr [rbp+90h]
    movaps [rbp+0A0h], xmm0
    movaps [rbp+0B0h], xmm1
    movaps [rbp+0C0h], xmm2
    movaps [rbp+0D0h], xmm3
    movaps [rbp+0E0h], xmm4
    movaps [rbp+0F0h], xmm5
    mov rax, [rbp+1B0h] ; original native RBP
    lea rcx, [rax-60h]  ; entire native mover row
    lea rdx, [rax+1B8h] ; native time/second object
    call creative_g_dispatch_route
    ; Replay exactly 48 8B 45 38 / 0F B6 48 3D against original RBP.
    mov rax, [rbp+1B0h]
    mov rax, [rax+38h]
    movzx ecx, byte ptr [rax+3Dh]
    mov [rbp+20h], rax
    mov [rbp+28h], rcx
    ldmxcsr [rbp+90h]
    movaps xmm0, [rbp+0A0h]
    movaps xmm1, [rbp+0B0h]
    movaps xmm2, [rbp+0C0h]
    movaps xmm3, [rbp+0D0h]
    movaps xmm4, [rbp+0E0h]
    movaps xmm5, [rbp+0F0h]
    movaps xmm6, [rbp+100h]
    movaps xmm7, [rbp+110h]
    movaps xmm8, [rbp+120h]
    movaps xmm9, [rbp+130h]
    movaps xmm10, [rbp+140h]
    movaps xmm11, [rbp+150h]
    movaps xmm12, [rbp+160h]
    movaps xmm13, [rbp+170h]
    movaps xmm14, [rbp+180h]
    movaps xmm15, [rbp+190h]
    mov rax, [rbp+20h]
    mov rcx, [rbp+28h]
    mov rdx, [rbp+30h]
    mov rbx, [rbp+38h]
    mov rsi, [rbp+40h]
    mov rdi, [rbp+48h]
    mov r8, [rbp+50h]
    mov r9, [rbp+58h]
    mov r10, [rbp+60h]
    mov r11, [rbp+68h]
    mov r12, [rbp+70h]
    mov r13, [rbp+78h]
    mov r14, [rbp+80h]
    mov r15, [rbp+88h]
    push qword ptr [rbp+1A8h]
    popfq
    ; Standard Win64 epilog, stable frame register throughout body.
    lea rsp, [rbp+1B0h]
    pop rbp
    ret
creative_g_dispatch_bridge ENDP
END
