#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// Endereço físico base da ponte HPS-to-FPGA Lightweight na DE1-SoC
#define BRIDGE_BASE       0xFF200000UL
#define BRIDGE_PAGE_SIZE  4096
#define BRIDGE_PAGE_OFF   0xFF200      // offset em páginas para o mmap2

// Offsets dos registradores PIO do coprocessador
#define REG_DATA_OUT  0x00   // leitura: resultado + flags de status
#define REG_SIGNALS   0x10   // escrita: enable, clr_operation, reset
#define REG_DATA_IN   0x20   // escrita: instrução de 32 bits

// Bits do registrador de sinais (pio_signals)
#define SIG_ENABLE   (1 << 0)
#define SIG_CLR_OP   (1 << 1)
#define SIG_RESET    (1 << 2)

// Bits do registrador de saída (pio_data_out)
#define BIT_DONE     (1 << 4)   // inferência ou operação concluída
#define BIT_BUSY     (1 << 5)   // coprocessador ocupado
#define BIT_ERROR    (1 << 6)   // erro detectado no hardware
#define MASK_DIGITO  0xF        // máscara para isolar os 4 bits do dígito predito

// Opcodes do protocolo de instrução de 32 bits
#define OP_STORE_IMG      0   // armazena pixel da imagem
#define OP_WEIGHTS_ADDR   1   // define endereço do peso (sem DONE)
#define OP_WEIGHTS_VAL    2   // armazena valor do peso
#define OP_STORE_BIAS     3   // armazena valor de bias
#define OP_STORE_BETA     4   // armazena coeficiente beta
#define OP_START          5   // dispara a inferência

// Tamanhos dos buffers de dados em bytes
#define TAM_IMG   784      // 28x28 pixels, 1 byte por pixel
#define TAM_BIAS  256      // 128 valores x 2 bytes (Q4.12)
#define TAM_BETA  2560     // 1280 valores x 2 bytes (Q4.12)
#define TAM_PESO  200704   // 100352 valores x 2 bytes (Q4.12)

// Funções exportadas pelo driver Assembly
int  mapear_fpga(void);
void reset_coprocessador(void);
void enviar_bias(void *buffer);
void enviar_beta(void *buffer);
void enviar_img(void *buffer);
void enviar_peso(void *buffer);
int  iniciar_inferencia(void);

#endif