/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
    .text
    .globl minlz_copy_forward_asm_amd64
#if defined(__MINGW32__) || defined(__MINGW64__)
    .def minlz_copy_forward_asm_amd64; .scl 2; .type 32; .endef
#endif
minlz_copy_forward_asm_amd64:
#if defined(_WIN64)
    pushq %rdi
    pushq %rsi
    movq %rcx, %rdi
    movq %rdx, %rsi
    movq %r8, %rcx
    rep movsb
    popq %rsi
    popq %rdi
    ret
#else
    movq %rdx, %rcx
    rep movsb
    ret
#endif

    .globl minlz_match_len_asm_amd64
#if defined(__MINGW32__) || defined(__MINGW64__)
    .def minlz_match_len_asm_amd64; .scl 2; .type 32; .endef
#endif
minlz_match_len_asm_amd64:
#if defined(_WIN64)
    xorl %eax, %eax
    testq %r8, %r8
    je .Lmatch_done_win
.Lmatch_qword_win:
    cmpq $8, %r8
    jb .Lmatch_byte_win
    movq (%rcx,%rax), %r9
    xorq (%rdx,%rax), %r9
    jne .Lmatch_diff_win
    addq $8, %rax
    subq $8, %r8
    jmp .Lmatch_qword_win
.Lmatch_diff_win:
    bsfq %r9, %r10
    shrq $3, %r10
    addq %r10, %rax
    ret
.Lmatch_byte_win:
    testq %r8, %r8
    je .Lmatch_done_win
.Lmatch_byte_loop_win:
    movzbl (%rcx,%rax), %r9d
    cmpb (%rdx,%rax), %r9b
    jne .Lmatch_done_win
    incq %rax
    decq %r8
    jne .Lmatch_byte_loop_win
.Lmatch_done_win:
    ret
#else
    xorl %eax, %eax
    testq %rdx, %rdx
    je .Lmatch_done_sysv
.Lmatch_qword_sysv:
    cmpq $8, %rdx
    jb .Lmatch_byte_sysv
    movq (%rdi,%rax), %r8
    xorq (%rsi,%rax), %r8
    jne .Lmatch_diff_sysv
    addq $8, %rax
    subq $8, %rdx
    jmp .Lmatch_qword_sysv
.Lmatch_diff_sysv:
    bsfq %r8, %r9
    shrq $3, %r9
    addq %r9, %rax
    ret
.Lmatch_byte_sysv:
    testq %rdx, %rdx
    je .Lmatch_done_sysv
.Lmatch_byte_loop_sysv:
    movzbl (%rdi,%rax), %r8d
    cmpb (%rsi,%rax), %r8b
    jne .Lmatch_done_sysv
    incq %rax
    decq %rdx
    jne .Lmatch_byte_loop_sysv
.Lmatch_done_sysv:
    ret
#endif

#if defined(__linux__) || defined(__ELF__)
    .section .note.GNU-stack,"",@progbits
#endif
