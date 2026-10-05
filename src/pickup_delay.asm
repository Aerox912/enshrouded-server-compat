EXTERN PickupInitialDelayOriginal:QWORD
.code
PickupInitialDelayDetour PROC
    ; This site already has valid r15=pickup settings and rsi=pickup state.
    ; Only XHL's private 200000123 ns marker bypasses the initial random wait.
    ; No registers or stack layout change; the original continuation sets flags.
    cmp DWORD PTR [r15+10h], 0bebc27bh
    jne pass_through
    cmp QWORD PTR [rsi+8], 0
    jne pass_through
    mov QWORD PTR [rsi+8], 1
pass_through:
    jmp QWORD PTR [PickupInitialDelayOriginal]
PickupInitialDelayDetour ENDP
END
