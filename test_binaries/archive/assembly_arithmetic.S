#
# Risc-V Assembler program to print "Hello World!"
# to stdout.
#
# from: https://smist08.wordpress.com/2019/09/07/risc-v-assembly-language-hello-world/
# a0-a2 - parameters to linux function services
# a7 - linux function number
#

.global _start      # Provide program starting address to linker

_start:
    addi x28, x0, 1
    addi x29, x0, 2
    add x10, x28, x29 #x10 should now be 3
# Setup the parameters to exit the program
# and then call Linux to do it.

    addi    a0, x0, 0   # Use 0 return code
    addi    a7, x0, 93  # Service command code 93 terminates
    ecall               # Call linux to terminate the program
