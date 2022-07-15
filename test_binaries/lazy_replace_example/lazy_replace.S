.global _start

_start:
	# x6 and x7 are not statically mapped, so they will be loaded for every access --> same for x0
	addi    x6, x0, 10
	addi    x7, x0, 11

	add     x6, x6, x7
	slli    x6, x6, 3
	xori    x7, x7, -1

	# exit()
    addi    a0, x0, 0
    addi    a7, x0, 93
    ecall
