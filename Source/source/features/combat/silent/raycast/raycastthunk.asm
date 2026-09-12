.code

public RaycastHook
public RaycastHookEnd

RaycastHook proc
    
    mov r10, 1111111111111111h
    test r10, r10
    jz inactive
    mov rax, 7FFFFFFFFFFFh
    cmp r10, rax
    ja inactive
    mov rax, 10000h
    cmp r10, rax
    jb inactive

    cmp dword ptr [r10], 0
    jz  inactive

    test r8, r8
    jz  inactive
    mov rax, 7FFFFFFFFFFFh
    cmp r8, rax
    ja inactive
    mov rax, 10000h
    cmp r8, rax
    jb inactive

    test r9, r9
    jz  inactive
    mov rax, 7FFFFFFFFFFFh
    cmp r9, rax
    ja inactive
    mov rax, 10000h
    cmp r9, rax
    jb inactive

    inc qword ptr [r10 + 40]

    movss xmm0, dword ptr [r8]
    subss xmm0, dword ptr [r10 + 88]
    movss xmm1, dword ptr [r8 + 4]
    subss xmm1, dword ptr [r10 + 92]
    movss xmm2, dword ptr [r8 + 8]
    subss xmm2, dword ptr [r10 + 96]

    mulss xmm0, xmm0
    mulss xmm1, xmm1
    mulss xmm2, xmm2
    addss xmm0, xmm1
    addss xmm0, xmm2

    comiss xmm0, dword ptr [r10 + 100]
    ja inactive

    cmp dword ptr [r10 + 4], 0
    jnz magic_path

    movss xmm0, dword ptr [r10 + 8]
    subss xmm0, dword ptr [r8]
    movss xmm1, dword ptr [r10 + 12]
    subss xmm1, dword ptr [r8 + 4]
    movss xmm2, dword ptr [r10 + 16]
    subss xmm2, dword ptr [r8 + 8]

    movss xmm3, dword ptr [r10 + 32]
    mov   eax,  3F800000h
    movd  xmm4, eax
    maxss xmm3, xmm4
    mulss xmm0, xmm3
    mulss xmm1, xmm3
    mulss xmm2, xmm3

    movss dword ptr [r10 + 20], xmm0
    movss dword ptr [r10 + 24], xmm1
    movss dword ptr [r10 + 28], xmm2

    lea   r9, [r10 + 20]
    jmp   inactive

magic_path:
    lea r8, [r10 + 56]
    lea r9, [r10 + 72]
    jmp inactive

inactive:
    mov rax, 2222222222222222h
    jmp rax

RaycastHookEnd::
    nop

RaycastHook endp

end