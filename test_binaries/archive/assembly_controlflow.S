# from: https://smist08.wordpress.com/2019/09/07/risc-v-assembly-language-hello-world/
# a0-a2 - parameters to linux function services
# a7 - linux function number
#

.global _start      # Provide program starting address to linker

_start:
    addi x28, x0, 10
    addi x29, x0, 1

    loop_head:
    beq x28, x0, loop_end

    addi  a0, x0, 1      # 1 = StdOut
    la    a1, helloworld # load address of helloworld
    addi  a2, x0, 14     # length of our string
    addi  a7, x0, 64     # linux write system call
    ecall

    sub x28, x28, x29

    j loop_head
    loop_end:
    # exit
    addi    a0, x0, 0   # Use 0 return code
    addi    a7, x0, 93  # Service command code 93 terminates
    ecall               # Call linux to terminate the program

.data
helloworld:      .ascii "Hello World!\n"
