// Test loader only: select a physical boot hart using SBI HSM, then enter the
// unmodified kernel at 0x40200000. U-Boot loads this Image at 0x49000000.
// The target hart word is at 0x49000410. No stack or kernel BSS is used here.
.section .text
.option norvc
.option norelax
.global _entry
_entry:
        j .Lprimary
        nop
        .dword 0x9000000
        .dword 0x1000
        .dword 0
        .word 2
        .word 0
        .dword 0
        .dword 0x5643534952
        .word 0x05435352
        .word 0
.Lprimary:
        csrci sstatus, 2
        csrw sie, zero
        csrw satp, zero
        sfence.vma zero, zero
        mv s0, a0
        la s1, .Larguments
        sd a0, 0(s1)
        sd a1, 8(s1)
        ld s2, 16(s1)
        li t0, 1
        bltu s2, t0, .Lerror
        li t0, 5
        bgeu s2, t0, .Lerror
        li s3, 1
.Lcheck:
        beq s3, s0, .Lnext
        mv a0, s3
        li a6, 2
        li a7, 0x48534d
        ecall
        bnez a0, .Lerror
        li t0, 1
        bne a1, t0, .Lerror
.Lnext:
        addi s3, s3, 1
        li t0, 5
        bltu s3, t0, .Lcheck
        beq s0, s2, .Lenter
        fence rw, rw
        mv a0, s2
        la a1, .Lselected
        mv a2, s1
        li a6, 0
        li a7, 0x48534d
        ecall
        bnez a0, .Lerror
        li a6, 1
        li a7, 0x48534d
        ecall
        j .Lerror
.Lselected:
        csrci sstatus, 2
        csrw sie, zero
        csrw satp, zero
        sfence.vma zero, zero
        mv s0, a0
        mv s1, a1
.Lwait_stopped:
        ld a0, 0(s1)
        li a6, 2
        li a7, 0x48534d
        ecall
        bnez a0, .Lerror
        li t0, 1
        bne a1, t0, .Lwait_stopped
.Lenter:
        fence rw, rw
        fence.i
        mv a0, s0
        ld a1, 8(s1)
        li t0, 0x40200000
        jr t0
.Lerror:
        la s4, .Lmessage
.Lprint:
        lbu a0, 0(s4)
        beqz a0, .Lpark
        li a7, 1
        ecall
        addi s4, s4, 1
        j .Lprint
.Lpark:
        wfi
        j .Lpark
.Lmessage:
        .asciz "HART_LOADER_FAIL\r\n"
.org 0x400
.Larguments:
        .dword 0
        .dword 0
        .dword 1
.org 0x1000
