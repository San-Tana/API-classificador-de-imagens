#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "driver.h"

/* ===========================================================
   Registradores MMIO do controlador VGA (offsets a partir da base)
   O driver Assembly do Marco 2 não mexe com o VGA, o controle dele 
   é feito aqui no C, escrevendo direto na ponte.
   =========================================================== */
#define VGA_STATUS    0x30   /* R: bit 0 = done                        */
#define VGA_SIGNALS   0x40   /* W: bit 0 = enable, bit 1 = reset       */
#define VGA_DATA_IN   0x50   /* W: posy[28:19] | posx[18:9] | RGB[8:0] */

/* Cores no formato de 9 bits RRRGGGBBB */
#define COR_PRETO     0x000
#define COR_BRANCO    0x1FF
#define COR_VERMELHO  0x1C0

/* Area de exibicao na tela: 28x28 escalado 8x -> 224x224 centralizado */
#define VGA_AREA_X0    48
#define VGA_AREA_Y0    8
#define VGA_AREA_SIZE  224
#define VGA_ESCALA     8

/* Ponteiro para a base mapeada da ponte HPS-FPGA (retornado por mapear_fpga) */
static volatile uint8_t *fpga_base = NULL;

/* Buffers estaticos para os parametros da rede */
static uint16_t buf_peso[100352];
static uint16_t buf_bias[128];
static uint16_t buf_beta[1280];
static uint8_t  buf_img[784];

/* Acesso MMIO ao VGA (escrita/leitura de 32 bits) */
static inline void vga_write(int offset, uint32_t valor) {
    *(volatile uint32_t *)(fpga_base + offset) = valor;
}
static inline uint32_t vga_read(int offset) {
    return *(volatile uint32_t *)(fpga_base + offset);
}

/* Primitivas de desenho no VGA */
/* Desenha um unico pixel na posicao (x, y) com a cor dada */
static void desenhar_pixel(int x, int y, int cor) {
    uint32_t instrucao = ((uint32_t)y << 19)   /* posy nos bits [28:19] */
                       | ((uint32_t)x << 9)    /* posx nos bits [18:9]  */
                       | (cor & 0x1FF);        /* cor nos bits [8:0]    */

    vga_write(VGA_DATA_IN, instrucao);

    vga_write(VGA_SIGNALS, 1);   /* enable = 1 */
    vga_write(VGA_SIGNALS, 0);   /* enable = 0 */
    
    while ((vga_read(VGA_STATUS) & 1) == 0);   /* aguarda done = 1 */
}

/* Preenche um bloco de lado x lado a partir de (x0, y0) */
static void desenhar_quadrado(int x0, int y0, int lado, int cor) {
    for (int dy = 0; dy < lado; dy++) {
        for (int dx = 0; dx < lado; dx++) {
            desenhar_pixel(x0 + dx, y0 + dy, cor);
        }
    }
}

/* Pinta a tela inteira de preto */
static void limpar_tela(void) {
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            desenhar_pixel(x, y, COR_PRETO);
        }
    }
}

/* Aplica um pulso de reset no controlador VGA com delays maiores */
static void reset_vga(void) {
    /* Garante que tudo esta desativado primeiro */
    vga_write(VGA_SIGNALS, 0);
    for (volatile int i = 0; i < 0x10000; i++);
    
    /* Pulsa o reset */
    vga_write(VGA_SIGNALS, 2);   /* reset = 1 */
    for (volatile int i = 0; i < 0x10000; i++);
    
    vga_write(VGA_SIGNALS, 0);   /* reset = 0 */
    for (volatile int i = 0; i < 0x10000; i++);
}

/* Exibe a imagem 28x28 escalada na tela, convertendo cada pixel
   (0..255) em uma cor cinza de 3 bits replicada em RGB */
static void exibir_imagem(uint8_t *buffer, int x0, int y0, int escala) {
    for (int py = 0; py < 28; py++) {
        for (int px = 0; px < 28; px++) {
            int valor = buffer[py * 28 + px];
            int g = valor >> 5;                 /* 8 bits -> 3 bits */
            int cor = (g << 6) | (g << 3) | g;  /* replica em R, G, B */
            desenhar_quadrado(x0 + px * escala, y0 + py * escala, escala, cor);
        }
    }
}

/* Carrega arquivos em formato .bin */
static int carregar_bin(const char *path, void *buffer, size_t tamanho) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERRO: nao foi possivel abrir %s\n", path);
        return 0;
    }
    fread(buffer, 1, tamanho, f);
    fclose(f);
    return 1;
}

/* Carrega imagens em formato .png */
static int carregar_png(const char *path, uint8_t *buffer) {
    int w, h, c;
    uint8_t *dados = stbi_load(path, &w, &h, &c, 1);   /* forca 1 canal */
    if (!dados) {
        fprintf(stderr, "ERRO: nao foi possivel ler %s\n", path);
        return 0;
    }
    if (w != 28 || h != 28) {
        fprintf(stderr, "ERRO: imagem deve ser 28x28 (recebido %dx%d)\n", w, h);
        stbi_image_free(dados);
        return 0;
    }
    memcpy(buffer, dados, 784);
    stbi_image_free(dados);
    return 1;
}

/* Calcula a latência */
static double tempo_ms(struct timespec t0, struct timespec t1) {
    return (t1.tv_sec - t0.tv_sec) * 1000.0
         + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

/* Modo 1: Inferencia a partir de arquivo PNG */
static void modo_arquivo(void) {
    char caminho[256];
    printf("\nCaminho do arquivo PNG: ");
    fflush(stdout);
    if (!fgets(caminho, sizeof(caminho), stdin)) return;
    caminho[strcspn(caminho, "\n")] = 0;
    if (strlen(caminho) == 0) {
        printf("Cancelado.\n");
        return;
    }
    /* Adiciona .png se necessário */
    size_t len = strlen(caminho);
    if (len < 4 || strcmp(caminho + len - 4, ".png") != 0) {
      strncat(path_csv, ".png", sizeof(path_csv) - strlen(caminho) - 1);
    }
    if (!carregar_png(caminho, buf_img)) return;

    limpar_tela();
    exibir_imagem(buf_img, VGA_AREA_X0, VGA_AREA_Y0, VGA_ESCALA);
    
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    enviar_img(buf_img);
    int resultado = iniciar_inferencia();
    clock_gettime(CLOCK_MONOTONIC, &t1);

    printf("\n=== Resultado ===\n");
    printf("Imagem:    %s\n", caminho);
    printf("Predicao:  %d\n", resultado);
    printf("Latencia:  %.2f ms\n", tempo_ms(t0, t1));

    reset_coprocessador();
}

/* Aplica um blur 3x3 (media dos vizinhos) para suavizar as bordas do
   desenho binario e aproxima-lo do estilo de pixels do MNIST original */
static void aplicar_blur(uint8_t *src, uint8_t *dst) {
    for (int y = 0; y < 28; y++) {
        for (int x = 0; x < 28; x++) {
            int soma = 0;
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int ny = y + dy;
                    int nx = x + dx;
                    if (nx >= 0 && nx < 28 && ny >= 0 && ny < 28) {
                        soma += src[ny * 28 + nx];
                        count++;
                    }
                }
            }
            dst[y * 28 + x] = soma / count;
        }
    }
}

/* Modo 2: Desenho com o mouse */
static void modo_desenho(void) {
    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERRO: nao foi possivel abrir /dev/input/mice\n");
        return;
    }

    limpar_tela();

    uint8_t desenho[784] = {0};
    int cursor_x = VGA_AREA_SIZE / 2;
    int cursor_y = VGA_AREA_SIZE / 2;
    int last_cell_x = -1, last_cell_y = -1;

    printf("\nModo desenho aberto.\n");
    printf("  - Mover o mouse: cursor vermelho 8x8\n");
    printf("  - Botao ESQUERDO: pinta a celula\n");
    printf("  - Botao DIREITO: encerra e classifica\n\n");
    
    unsigned char buf[3];

    /* Garante que o loop principal será executado apenas quando o botão direito estiver solto */
    while (read(fd, buf, 3) == 3 && (buf[0] & 0x07)); 

    /* Outra garantia que o programa irá detectar apenas a transição entre solto->pressionado do botão direito */
    int prev_buttons = 0;

    while (read(fd, buf, 3) == 3) {
        int buttons = buf[0] & 0x07;
        int dx =  (int8_t)buf[1];
        int dy = -(int8_t)buf[2];

        cursor_x += dx;
        cursor_y += dy;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x >= VGA_AREA_SIZE) cursor_x = VGA_AREA_SIZE - 1;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y >= VGA_AREA_SIZE) cursor_y = VGA_AREA_SIZE - 1;
        
        /* Converte a posição do cursor no vga para a posição da celula do desenho */
        int cell_x = cursor_x / VGA_ESCALA;
        int cell_y = cursor_y / VGA_ESCALA;
        int cell_mudou = (cell_x != last_cell_x || cell_y != last_cell_y); /* Verifica se a celula atual é diferente da anterior */

        /* Botao direito encerra: restaura a ultima celula e sai */
        if (!(prev_buttons & 2) && (buttons & 2)) {
            if (last_cell_x >= 0) {
                int cor = desenho[last_cell_y * 28 + last_cell_x] ? COR_BRANCO : COR_PRETO;
                desenhar_quadrado(VGA_AREA_X0 + last_cell_x * VGA_ESCALA,
                                  VGA_AREA_Y0 + last_cell_y * VGA_ESCALA,
                                  VGA_ESCALA, cor);
            }
            break;
        }

        /* Ao mudar de celula, restaura a cor real da anterior */
        if (cell_mudou) {
            if (last_cell_x >= 0) {
                int cor = desenho[last_cell_y * 28 + last_cell_x] ? COR_BRANCO : COR_PRETO;
                desenhar_quadrado(VGA_AREA_X0 + last_cell_x * VGA_ESCALA,
                                  VGA_AREA_Y0 + last_cell_y * VGA_ESCALA,
                                  VGA_ESCALA, cor);
            }
            
            /* Atualiza a última celula onde o cursor estava */
            last_cell_x = cell_x;
            last_cell_y = cell_y;
        }

        if (buttons & 1) {
            /* Botao esquerdo pinta a celula de branco */
            desenho[cell_y * 28 + cell_x] = 255;
            desenhar_quadrado(VGA_AREA_X0 + cell_x * VGA_ESCALA,
                              VGA_AREA_Y0 + cell_y * VGA_ESCALA,
                              VGA_ESCALA, COR_BRANCO);
        } else if (cell_mudou) {
            /* Sem botao, mostra o cursor vermelho na celula atual */
            desenhar_quadrado(VGA_AREA_X0 + cell_x * VGA_ESCALA,
                              VGA_AREA_Y0 + cell_y * VGA_ESCALA,
                              VGA_ESCALA, COR_VERMELHO);
        }

        /* Atualiza o valor do último evento de botão */ 
        prev_buttons = buttons;
    }
    close(fd);

    /* Aplica blur 3x3 para suavizar bordas e aproximar do estilo MNIST */
    uint8_t desenho_blur[784];
    aplicar_blur(desenho, desenho_blur);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    enviar_img(desenho_blur);
    int resultado = iniciar_inferencia();
    clock_gettime(CLOCK_MONOTONIC, &t1);

    printf("\n=== Resultado ===\n");
    printf("Predicao:  %d\n", resultado);
    printf("Latencia:  %.2f ms\n", tempo_ms(t0, t1));

    reset_coprocessador();
}

/* Modo 3: Benchmark (dataset interno) */
typedef struct {
    char arquivo[64];
    int  esperado;
    int  predito;
    int  acerto;
    double tempo_ms;
} Resultado;

static void modo_benchmark(void) {
    char buf[64];
    char path_csv[256] = "casoteste.csv";
    char log_csv[256] = "benchmark.csv";

    char linha[300];
    char caminho[256];
    int esperado;

    printf("\nCaminho do CSV para o testbench (Enter para casoteste.csv): ");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        buf[strcspn(buf, "\n")] = 0;
        strncpy(path_csv, buf, sizeof(path_csv) - 1);
        path_csv[sizeof(path_csv) - 1] = 0;

        /* Adiciona .csv se necessário */
        size_t len = strlen(path_csv);
        if (len < 4 || strcmp(path_csv + len - 4, ".csv") != 0) {
            strncat(path_csv, ".csv", sizeof(path_csv) - strlen(path_csv) - 1);
        }
    }

    FILE *f = fopen(path_csv, "r");
    if (!f) {
        fprintf(stderr, "\nERRO: nao foi possivel abrir %s\n", path_csv);
        return;
    }

    printf("Arquivo CSV de log (Enter para benchmark.csv): ");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        buf[strcspn(buf, "\n")] = 0;
        strncpy(log_csv, buf, sizeof(log_csv) - 1);
        log_csv[sizeof(log_csv) - 1] = 0;

        size_t len = strlen(log_csv);
        if (len < 4 || strcmp(path_csv + len - 4, ".csv") != 0) {
            strncat(log_csv, ".csv", sizeof(log_csv) - strlen(log_csv) - 1);
        }
    }

    int n_imagens = 0;
    /* Pula cabeçalho */
    fgets(linha, sizeof(linha), f);
    /* Conta quantas linhas existem */
    while (fgets(linha, sizeof(linha), f)) {
        n_imagens++;
    }

    if (n_imagens == 0) {
        fprintf(stderr, "\nCSV sem imagens.\n");
        fclose(f);
        return;
    }

    /* Volta para o início */
    rewind(f);
    /* Pula cabeçalho novamente */
    fgets(linha, sizeof(linha), f);

    printf("\nRodando benchmark com %d imagens...\n\n", n_imagens);

    Resultado *resultados = malloc(n_imagens * sizeof(Resultado));
    if (!resultados) {
        fprintf(stderr, "ERRO: falha ao alocar memoria.\n");
        fclose(f);
        return;
    }

    limpar_tela();

    int acertos = 0;
    int i = 0;

    struct timespec inicio, fim, t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    while (fgets(linha, sizeof(linha), f)) {
        if (sscanf(linha, "%255[^,],%d", caminho, &esperado) != 2) {
            fprintf(stderr, "Linha invalida: %s", linha);
            continue;
        }

        if (!carregar_png(caminho, buf_img)) continue;

        exibir_imagem(buf_img, VGA_AREA_X0, VGA_AREA_Y0, VGA_ESCALA);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        enviar_img(buf_img);
        int predito = iniciar_inferencia();
        clock_gettime(CLOCK_MONOTONIC, &t1);

        int acerto = (predito == esperado);

        strncpy(resultados[i].arquivo, caminho, sizeof(resultados[i].arquivo)-1);
        resultados[i].arquivo[sizeof(resultados[i].arquivo)-1] = '\0';
        resultados[i].esperado = esperado;
        resultados[i].predito  = predito;
        resultados[i].acerto   = acerto;
        resultados[i].tempo_ms = tempo_ms(t0, t1);

        if (acerto) acertos++;

        printf("[%3d/%d] %-8s -> %d (esperado %d) %s  %.2f ms\n",
               i + 1, n_imagens, resultados[i].arquivo,
               predito, esperado, acerto ? "OK" : "ERRO",
               resultados[i].tempo_ms);
        i++;

        reset_coprocessador();
    }
    clock_gettime(CLOCK_MONOTONIC, &fim);
    fclose(f);
    
    
    int testes_executados = i;
    if (testes_executados == 0) {
        fprintf(stderr, "\nNenhuma imagem foi processada.\n");
        free(resultados);
    return;
    }
    /* Falhas de leitura */
    int falhas = n_imagens - testes_executados;
    
    /* Tempo total */
    double total = tempo_ms(inicio, fim);

    /* Calcula media */
    double soma = 0.0;
    for (int i = 0; i < testes_executados; i++) soma += resultados[i].tempo_ms;
    double media = soma / testes_executados;

    /* Calcula desvio padrao amostral */
    double soma_sq = 0.0;
    for (int i = 0; i < testes_executados; i++) {
        double d = resultados[i].tempo_ms - media;
        soma_sq += d * d;
    }
    double desvio = (testes_executados > 1) ? sqrt(soma_sq / (testes_executados - 1)) : 0.0;
    
    /* Acurácia e throughput */
    double acuracia = 100.0 * acertos / testes_executados;
    double throughput = 1000.0 * testes_executados / total;
    
    /* Coloca os resultados em uma String para exibição no terminal e para salvar no log */
    char resultado_benchmark[1024];
    snprintf(resultado_benchmark, sizeof(resultado_benchmark),
            "\n=== Resultado do Benchmark ===\n"
            "Imagens processadas:  %d\n"
            "Falhas de leitura:    %d\n"
            "Acertos:              %d/%d\n"
            "Acuracia:             %.1f%%\n"
            "Latencia media:       %.2f ms\n"
            "Desvio padrao:        %.2f ms\n"
            "Tempo total:          %.2f ms\n"
            "Throughput:           %.1f imagens/s\n",
            testes_executados, falhas, acertos, testes_executados, acuracia, media, desvio, total, throughput);

    printf(resultado_benchmark);

    /* Salva CSV */
    FILE *log = fopen(log_csv, "w");
    if (log) {
        fprintf(log, "imagem,esperado,predito,acerto,tempo_ms\n");
        for (int i = 0; i < testes_executados; i++) {
            fprintf(log, "%s,%d,%d,%d,%.3f\n",
                    resultados[i].arquivo,
                    resultados[i].esperado,
                    resultados[i].predito,
                    resultados[i].acerto,
                    resultados[i].tempo_ms);
        }
        fprintf(log, resultado_benchmark); /* Salva os resultados finais no final do log */

        fclose(log);
        printf("Log salvo em:         %s\n", log_csv);
    } else {
        fprintf(stderr, "AVISO: nao foi possivel salvar %s\n", log_csv);
    }

    free(resultados); /* libera a memória */
}

/* Inicializacao do hardware (executada uma unica vez) */
static int inicializar(void) {
    printf("Carregando parametros da rede do disco...\n");
    if (!carregar_bin("data/w_in_q.bin", buf_peso, sizeof(buf_peso))) return 0;
    if (!carregar_bin("data/b_q.bin",    buf_bias, sizeof(buf_bias))) return 0;
    if (!carregar_bin("data/beta_q.bin", buf_beta, sizeof(buf_beta))) return 0;

    printf("Mapeando a FPGA...\n");
    int base = mapear_fpga();
    if (base == 0 || base == -1) {
        fprintf(stderr, "ERRO: nao foi possivel mapear /dev/mem (rodar com sudo)\n");
        return 0;
    }
    /* Atribui com segurança ao ponteiro global da VGA */
    fpga_base = (volatile uint8_t *)(uintptr_t)base;

    reset_coprocessador();
    reset_vga();
    limpar_tela();

    printf("Enviando parametros ao coprocessador...\n");
    enviar_bias(buf_bias);
    enviar_beta(buf_beta);
    enviar_peso(buf_peso);
    printf("Pronto.\n\n");
    return 1;
}

/* Exibe as opções como um menu interativo */
static void mostrar_menu(void) {
    printf("=========================================\n");
    printf("  Classificador de Digitos MNIST\n");
    printf("=========================================\n");
    printf("  1. Classificar uma imagem PNG\n");
    printf("  2. Desenhar com o mouse\n");
    printf("  3. Benchmark\n");
    printf("  0. Sair\n");
    printf("=========================================\n");
    printf("Escolha: ");
    fflush(stdout);
}

int main(void) {
    if (!inicializar()) return 1;

    char input[16];
    while (1) {
        mostrar_menu();
        if (!fgets(input, sizeof(input), stdin)) break;
        
        /* Escolha de modo */
        switch (input[0]) {
            case '1': modo_arquivo(); break;
            case '2': modo_desenho(); break;
            case '3': modo_benchmark(); break;
            case '0': case 'q': case 'Q':
                printf("\nEncerrando.\n");
                return 0;
            case '\n':
                continue;
            default:
                printf("Opcao invalida.\n");
        }

        printf("\nPressione Enter para voltar ao menu...");
        fflush(stdout);
        fgets(input, sizeof(input), stdin);
        printf("\n");
    }
    return 0;
}
