.syntax unified
.arm

@ Buffers em memória (mantidos por compatibilidade, mas não usados pelas funções de envio).
@ A leitura dos arquivos agora é feita no C, e as funções recebem o ponteiro do buffer em R0.
.data
    dev_mem_path: .asciz "/dev/mem"
    base_mmio:    .word 0          @ endereço virtual da ponte HPS-FPGA após o mmap

.text

@ Abre /dev/mem e mapeia a ponte HPS-to-FPGA lightweight no espaço de endereçamento do processo.
@ O endereço mapeado é salvo em base_mmio para uso posterior e também retornado em R0.
.type mapear_fpga, %function
.global mapear_fpga
mapear_fpga:
    PUSH {R4-R5, R7, LR}
    LDR R5, =0xFF200               @ offset de página: 0xFF200000 / 4096
    LDR R0, =dev_mem_path
    MOV R1, #2                     @ O_RDWR
    MOV R7, #5                     @ syscall open
    SVC 0                          @ R0 = file descriptor de /dev/mem

    MOV R4, R0                     @ salva o fd antes do mmap sobrescrever R0
    MOV R0, #0                     @ addr = NULL (kernel escolhe o endereço)
    MOV R1, #4096                  @ tamanho da região mapeada
    MOV R2, #3                     @ PROT_READ | PROT_WRITE
    MOV R3, #1                     @ MAP_SHARED
    MOV R7, #192                   @ syscall mmap2
    SVC 0                          @ R0 = endereço virtual mapeado

    LDR R5, =base_mmio
    STR R0, [R5]                   @ salva o endereço base para as demais funções

    POP {R4-R5, R7, LR}
    BX LR

@ Envia uma instrução de 32 bits ao coprocessador via data_in,
@ ativa o enable e aguarda o flag DONE ser levantado antes de retornar.
enviar_instrucao:
    PUSH {R1, LR}
    STR R0, [R4, #0x20]            @ escreve a instrução no registrador data_in
    MOV R1, #1
    STR R1, [R4, #0x10]            @ ativa enable (bit 0 de pio_signals)

esperar_done:
    LDR R1, [R4, #0x00]            @ lê data_out
    TST R1, #16                    @ testa o bit 4 (flag DONE)
    BEQ esperar_done               @ aguarda enquanto DONE = 0

    MOV R1, #0
    STR R1, [R4, #0x10]            @ desativa enable

    POP {R1, LR}
    BX LR

@ Recebe em R0 o ponteiro do buffer de bias e envia os 128 valores ao coprocessador (opcode 3).
@ Cada instrução tem o valor em bits [25:10] e o índice em bits [9:3].
.type enviar_bias, %function
.global enviar_bias
enviar_bias:
    PUSH {r4-r7, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R2, R0                     @ R2 aponta para o buffer recebido do C
    MOV R5, #0                     @ índice inicial

loop_bias:
    CMP R5, #128
    BGE fim_bias

    LDRH R6, [R2]
    REV16 R6, R6                   @ corrige big-endian para little-endian
    ADD R2, R2, #2
    LSL R6, R6, #10                @ posiciona valor nos bits [25:10]
    MOV R7, R5
    LSL R7, R7, #3                 @ posiciona índice nos bits [9:3]
    ORR R0, R6, R7
    ORR R0, R0, #3                 @ opcode STORE_BIAS = 3

    BL enviar_instrucao
    ADD R5, R5, #1
    B loop_bias

fim_bias:
    POP {r4-r7, LR}
    BX LR

@ Recebe em R0 o ponteiro do buffer de beta e envia os 1280 coeficientes ao coprocessador (opcode 4).
@ Cada instrução tem o valor em bits [29:14] e o índice em bits [13:3].
.type enviar_beta, %function
.global enviar_beta
enviar_beta:
    PUSH {r4-r7, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R2, R0                     @ R2 aponta para o buffer recebido do C
    MOV R5, #0

loop_beta:
    CMP R5, #1280
    BGE fim_beta

    LDRH R6, [R2]
    REV16 R6, R6
    ADD R2, R2, #2
    LSL R6, R6, #14                @ posiciona valor nos bits [29:14]
    MOV R7, R5
    LSL R7, R7, #3                 @ posiciona índice nos bits [13:3]
    ORR R0, R6, R7
    ORR R0, R0, #4                 @ opcode STORE_BETA = 4

    BL enviar_instrucao
    ADD R5, R5, #1
    B loop_beta

fim_beta:
    POP {r4-r7, LR}
    BX LR

@ Recebe em R0 o ponteiro do buffer da imagem e envia os 784 pixels ao coprocessador (opcode 0).
@ Cada pixel tem 1 byte; o valor fica em bits [20:13] e o índice em bits [12:3].
.type enviar_img, %function
.global enviar_img
enviar_img:
    PUSH {r4-r7, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R2, R0                     @ R2 aponta para o buffer recebido do C
    MOV R5, #0

loop_img:
    CMP R5, #784
    BGE fim_img

    LDRB R6, [R2]                  @ lê 1 byte (pixel sem sinal)
    ADD R2, R2, #1
    LSL R6, R6, #13                @ posiciona valor nos bits [20:13]
    MOV R7, R5
    LSL R7, R7, #3                 @ posiciona índice nos bits [12:3]
    ORR R0, R6, R7
    ORR R0, R0, #0                 @ opcode STORE_IMG = 0

    BL enviar_instrucao
    ADD R5, R5, #1
    B loop_img

fim_img:
    POP {r4-r7, LR}
    BX LR

@ Versão sem polling de DONE — usada apenas para STORE_WEIGHTS_ADDR (opcode 1),
@ que não gera sinal de conclusão pois retorna ao IDLE sem passar pela memória.
enviar_instrucao_sem_done:
    STR R0, [R4, #0x20]
    MOV R1, #1
    STR R1, [R4, #0x10]           @ pulso de enable
    MOV R1, #0
    STR R1, [R4, #0x10]
    BX LR

@ Recebe em R0 o ponteiro do buffer de pesos e envia os 100352 pesos W_in em pares de instruções:
@ opcode 1 (endereço, sem DONE) seguido de opcode 2 (valor, com DONE).
@ O índice não cabe em uma instrução só, por isso é dividido em dois envios.
.type enviar_peso, %function
.global enviar_peso
enviar_peso:
    PUSH {r4-r7, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R2, R0                     @ R2 aponta para o buffer recebido do C
    MOV R5, #0

loop_peso:
    CMP R5, #100352
    BGE fim_peso

    MOV R7, R5
    LSL R7, R7, #3
    ORR R0, R7, #1                 @ instrução de endereço: índice | opcode 1
    BL enviar_instrucao_sem_done

    LDRH R6, [R2]
    REV16 R6, R6
    ADD R2, R2, #2
    LSL R6, R6, #3
    ORR R0, R6, #2                 @ instrução de valor: dado | opcode 2
    BL enviar_instrucao

    ADD R5, R5, #1
    B loop_peso

fim_peso:
    POP {r4-r7, LR}
    BX LR

@ Limpa o flag DONE, dispara a inferência (opcode 5) e aguarda a conclusão.
@ Retorna em R0 os 4 bits do dígito predito (0-9).
.type iniciar_inferencia, %function
.global iniciar_inferencia
iniciar_inferencia:
    PUSH {R4, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R1, #2
    STR R1, [R4, #0x10]            @ pulso de clr_operation para garantir DONE = 0
    MOV R1, #0
    STR R1, [R4, #0x10]

    MOV R0, #5
    STR R0, [R4, #0x20]            @ instrução START (opcode 5)
    MOV R1, #1
    STR R1, [R4, #0x10]            @ ativa enable

esperar_inferencia:
    LDR R1, [R4, #0x00]
    TST R1, #16                    @ aguarda flag DONE (bit 4) = 1
    BEQ esperar_inferencia

    LDR R0, [R4, #0x00]
    MOV R1, #0
    STR R1, [R4, #0x10]            @ desativa enable
    AND R0, R0, #15                @ isola os 4 bits do dígito predito

    POP {R4, LR}
    BX LR

@ Aplica um pulso de reset no coprocessador e aguarda o hardware estabilizar.
.type reset_coprocessador, %function
.global reset_coprocessador
reset_coprocessador:
    PUSH {R4, LR}
    LDR R4, =base_mmio
    LDR R4, [R4]

    MOV R1, #4
    STR R1, [R4, #0x10]            @ ativa bit 2 (rst) de pio_signals
    MOV R0, #0x5000                @ contador de delay

delay_reset:
    SUBS R0, #1
    BNE delay_reset

    MOV R1, #0
    STR R1, [R4, #0x10]            @ libera o reset
    POP {R4, LR}
    BX LR