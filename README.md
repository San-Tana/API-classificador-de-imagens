# API-classificador-de-imagens

<div align="center">
<h1>

## Sumário

</h1>
</div>

- [Introdução](#introdução)
- [Requisitos Principais](#requisitos-principais)
  - [Entrada e Saída](#entrada-e-saída)
  - [Interface MMIO com o Coprocessador](#interface-mmio-com-o-coprocessador)
  - [Conjunto de Instruções (ISA)](#conjunto-de-instruções-isa)
  - [Arquivos Binários de Entrada](#arquivos-binários-de-entrada)
- [Fundamentação Teórica](#fundamentação-teórica)
  - [Mapeamento de Memória (MMIO)](#mapeamento-de-memória-mmio)
  - [System Calls Linux em Assembly ARM](#system-calls-linux-em-assembly-arm)
  - [Representação em Ponto Fixo Q4.12](#representação-em-ponto-fixo-q412)
  - [Protocolo de Comunicação com o Coprocessador](#protocolo-de-comunicação-com-o-coprocessador)
  - [Interworking Thumb–ARM](#interworking-thumbarm)
- [Metodologia](#Metodologia)
- [Descrição da Solução](#descrição-da-solução)
  - [Arquitetura Geral do Driver](#arquitetura-geral-do-driver)
  - [mapear_fpga](#mapear_fpga)
  - [carregar_arquivo](#carregar_arquivo)
  - [enviar_instrucao e enviar_instrucao_sem_done](#enviar_instrucao-e-enviar_instrucao_sem_done)
  - [enviar_bias, enviar_beta, enviar_img](#enviar_bias-enviar_beta-enviar_img)
  - [enviar_peso](#enviar_peso)
  - [iniciar_inferencia](#iniciar_inferencia)
  - [reset_coprocessador](#reset_coprocessador)
  - [Header driver.h](#header-driverh)
  - [Aplicação em C — main.c](#aplicação-em-c--mainc)
- [Modo de Uso](#modo-de-uso)
  - [Estrutura de Diretórios](#estrutura-de-diretórios)
  - [Compilação](#compilação)
  - [Execução](#execução)
- [Problemas Encontrados e Correções](#problemas-encontrados-e-correções)
- [Resultados](#resultados)
- [Conclusão](#conclusão)
- [Referências](#referências)

---

<div align="center">
<h1>

## Introdução

</h1>
</div>

  Este documento descreve o desenvolvimento do Marco 2 de um sistema embarcado para classificação de dígitos numéricos. O sistema completo combina um coprocessador implementado em Verilog na FPGA Cyclone V da placa DE1-SoC, desenvolvido no Marco 1, com um driver em linguagem Assembly ARMv7 executado no processador ARM (HPS) sob Linux, integrado a uma aplicação em C. O coprocessador base para o driver em questão foi projetado por Maike de Oliveira, seu repositório original pode ser encontrado em: github.com/DestinyWolf/Problema_SD_2026_1 e é recomendado para aqueles que queiram se aprofundar no funcionamento do coprocessador. Entretanto, algumas alterações foram feitas para que o driver fosse capaz de se conectar a ele. O modelo alterado esta disponível neste repositório no diretório `coprocessador/`.

  O objetivo do Marco 2 é desenvolver o driver responsável por toda a comunicação entre o processador ARM e o coprocessador na FPGA, além de uma interface de programação de aplicações (API) em C que controla o fluxo de inferência. O driver é implementado como uma biblioteca de funções em Assembly ARMv7, chamadas diretamente pelo programa C. Ele carrega os parâmetros da rede neural a partir de arquivos binários no disco, os envia ao coprocessador via MMIO, dispara a inferência e retorna o dígito predito.

<div align="center">
<h1>

## Requisitos Principais

</h1>
</div>

### Entrada e Saída

O sistema recebe como entrada quatro arquivos binários localizados no diretório `data/`, contendo os parâmetros da rede neural ELM e as imagens a serem classificadas. A saída é o dígito predito (0 a 9), retornado como valor inteiro pela função `iniciar_inferencia` e impresso pelo programa `main.c`.

### Interface MMIO com o Coprocessador

O coprocessador é acessado via mapeamento de memória a partir do endereço físico `0xFF200000`. Três registradores PIO são utilizados:

| Offset | Registrador    | Função                                          |
|--------|----------------|-------------------------------------------------|
| `0x00` | `pio_data_out` | Leitura do resultado e flags de status          |
| `0x10` | `pio_signals`  | enable (bit 0), clr_operation (bit 1), rst (bit 2) |
| `0x20` | `pio_data_in`  | Escrita da instrução de 32 bits                 |

O mapeamento é feito via chamada de sistema (system call, ou syscall) `mmap2` (syscall 192), acessando `/dev/mem` com permissão de leitura e escrita. O offset passado ao mmap2 é `0xFF200` — o endereço físico dividido pelo tamanho de página de 4096 bytes.

### Conjunto de Instruções (ISA)

Cada dado é enviado ao coprocessador como uma instrução de 32 bits. Os 3 bits menos significativos definem o opcode:

| Opcode | Instrução              | Descrição                                                        |
|--------|------------------------|------------------------------------------------------------------|
| `0`    | `STORE_IMG`            | Armazena pixel da imagem. Índice em [12:3], valor em [20:13]    |
| `1`    | `STORE_WEIGHTS_ADDR`   | Define o endereço do próximo peso. Índice em [19:3]             |
| `2`    | `STORE_WEIGHTS_VALUE`  | Armazena o valor do peso. Dado em [18:3]                        |
| `3`    | `STORE_BIAS`           | Armazena um bias. Índice em [9:3], valor em [25:10]             |
| `4`    | `STORE_BETA`           | Armazena um coeficiente beta. Índice em [13:3], valor em [29:14]|
| `5`    | `START`                | Dispara a inferência                                            |

O bit 4 de `pio_data_out` é a flag DONE. O driver faz polling nesse bit após cada instrução enviada. Para os pesos W_in (100.352 elementos), o envio é dividido em dois: primeiro o endereço com opcode 1 sem esperar DONE, depois o valor com opcode 2 esperando DONE.

### Arquivos Binários de Entrada

Os arquivos devem estar no diretório `data/`:

| Arquivo           | Tamanho      | Conteúdo                        |
|-------------------|--------------|---------------------------------|
| `data/img.bin`    | 784 bytes    | Pixels brutos 8 bits por pixel  |
| `data/w_in_q.bin` | 200.704 bytes| Pesos W_in em Q4.12 (int16)     |
| `data/b_q.bin`    | 256 bytes    | Bias em Q4.12 (int16)           |
| `data/beta_q.bin` | 2.560 bytes  | Pesos beta em Q4.12 (int16)     |

Em caso de teste com 100 imagens, os arquivos de imagem devem ser nomeados `data/0.bin` até `data/99.bin`. Os arquivos de imagem usados nos testes de predição para definir a acurácia da inferencia já estão no diretório `data/`, sendo 10 imagens para cada digito de 0 à 9. 

<div align="center">
<h1>

## Fundamentação Teórica

</h1>
</div>

### Mapeamento de Memória (MMIO)

Em sistemas Linux com FPGA, a forma padrão de o processador acessar os registradores do hardware é através de mapeamento de memória (Memory-Mapped I/O). O arquivo especial `/dev/mem` expõe o espaço de endereçamento físico do sistema como se fosse um arquivo, permitindo que um processo em nível de usuário mapeie regiões físicas para o seu espaço de endereçamento virtual através da syscall `mmap`.

Na DE1-SoC, a ponte HPS-to-FPGA Lightweight mapeia os periféricos da FPGA a partir do endereço físico `0xFF200000`. Após o mapeamento, o driver escreve e lê nesses endereços usando instruções `STR` e `LDR` comuns do ARM, como se fossem posições de memória normais.

### Mapeamento da ponte HPS-to-FPGA via mmap2
<img width="370" height="360" alt="image" src="https://github.com/user-attachments/assets/875105c8-0139-4819-9b06-ca3af995745f" />


### System Calls Linux em Assembly ARM

Uma chamada de sistema é uma rotina que permite que um aplicativo de usuário solicite ações que requerem privilégios especiais. No código em Assembly, essas chamadas são feitas nas funções `mapear_fpga` e `carregar_arquivo`, carregando o número da syscall no registrador R7, os argumentos nos registradores R0 a R5, e executando a instrução `SVC 0`. As syscalls utilizadas no driver são:

| Syscall | Número | Uso no driver                          |
|---------|--------|----------------------------------------|
| `open`  | 5      | Abre `/dev/mem` e os arquivos binários |
| `read`  | 3      | Lê o conteúdo dos arquivos             |
| `close` | 6      | Fecha os descritores de arquivo        |
| `mmap2` | 192    | Mapeia o espaço de endereço da FPGA    |

### Representação em Ponto Fixo Q4.12

Os parâmetros da rede neural são armazenados no formato Q4.12: inteiros de 16 bits com sinal, onde os 12 bits menos significativos representam a parte fracionária e os 4 bits mais significativos representam a parte inteira, incluindo sinal. Os arquivos binários foram gerados em big-endian. Por isso, após cada `LDRH` (Carrega 2 bytes em little-endian), é aplicada a instrução `REV16` para inverter a ordem dos bytes antes de montar a instrução para o coprocessador.

### Protocolo de Comunicação com o Coprocessador

O handshake para cada instrução funciona da seguinte forma:

1. Escreve a instrução de 32 bits no registrador `data_in` (offset `0x20`)
2. Ativa o sinal `enable` escrevendo `1` no `pio_signals` (offset `0x10`)
3. Aguarda em polling até o bit 4 de `data_out` (flag DONE) ser 1
4. Desativa o `enable` escrevendo `0` no `pio_signals`

Para `STORE_WEIGHTS_ADDR` (opcode 1), o coprocessador retorna ao estado IDLE sem passar pela memória, portanto não gera sinal DONE. Essa instrução é enviada sem polling.

### Fluxo de envio de instrução ao coprocessador
<img width="100" height="400" alt="image" src="https://github.com/user-attachments/assets/a2749b8b-3a98-465f-a768-27c5582b7e82" />

### Interworking Thumb–ARM

O GCC por padrão compila código C em Thumb-2, enquanto o assembly do driver é escrito em ARM (A32). Quando código Thumb chama uma função ARM, o processador precisa trocar de modo, isso é chamado de interworking. Para funcionar corretamente sem a flag -marm, o arquivo assembly precisa de três declarações:

- `.syntax unified` — Ativa a sintaxe ARM unificada (UAL)
- `.arm` — Declara que o código a seguir é ARM, não Thumb
- `.type funcname, %function` — Usada antes de cada função exportada. Informa ao linker que é uma função ARM, fazendo com que ele gere automaticamente os stubs de interworking para as chamadas vindas do C

<div align="center">
<h1>

## Metodologia

</h1>
</div>

A metodologia usada no projeto foi a do PBL (Problem Based Learning) com reuniões em sessões tutorial, onde a turma define meta e discute a solução do problema. Os roteiros disponibilizados pelos professores ajudaram no desenvolvimento do projeto, o LAB 0 foi importante para entender o funcionamento do terminal Linux com a placa DE1-SoC e o Lab 2 foi fundamental para compreendermos a integração FPGA-HPS, além de ter sido importante na utilização do coprocessador que foi disponibilizado por Maike. Durante as sessões tutoriais, tópicos cruciais foram debatidos, como a utilização das chamadas de sistema, o mapeamento da memória, polling ou interrupção, entre outros.

O driver foi desenvolvido em Assembly ARMv7, junto a uma API em C composta pelo arquivo principal `main.c` e pelo header driver.h para estruturar a integração entre as duas linguagens, que declara as funções exportadas pelo assembly e centraliza as constantes do hardware. Essa separação permitiu que o C controlasse o fluxo da aplicação enquanto o assembly ficou responsável por toda a comunicação com o coprocessador.

<div align="center">
<h1>
  
## Descrição da Solução

</h1>
</div>

### Arquitetura Geral do Driver

O driver é organizado como uma biblioteca de funções em Assembly ARMv7, linkada diretamente com o programa C. O ponto de entrada é o `main` do C. O endereço da ponte FPGA é obtido por `mapear_fpga` através do Syscall e armazenado em uma variável global (`base_mmio`) na seção `.data` do assembly, acessível por todas as funções.

| Função                       | Responsabilidade                                                |
|------------------------------|-----------------------------------------------------------------|
| `mapear_fpga`                | Abre /dev/mem, mapeia a ponte FPGA, salva o endereço base      |
| `carregar_arquivo`           | Lê um arquivo binário para um buffer em memória                |
| `enviar_instrucao`           | Envia uma instrução e aguarda DONE                             |
| `enviar_instrucao_sem_done`  | Envia uma instrução sem aguardar (opcode 1)                    |
| `enviar_bias`                | Carrega e envia os 128 valores de bias                         |
| `enviar_beta`                | Carrega e envia os 1280 coeficientes beta                      |
| `enviar_img`                 | Carrega e envia os 784 pixels da imagem                        |
| `enviar_peso`                | Carrega e envia os 100.352 pesos W_in                          |
| `iniciar_inferencia`         | Limpa DONE, dispara START, aguarda conclusão, retorna o dígito |
| `reset_coprocessador`        | Pulsa o sinal de reset da FPGA                                 |

### Hierarquia de chamadas do driver
<img width="500" height="375" alt="image" src="https://github.com/user-attachments/assets/0807f077-8b50-43c0-a939-42059d6dff70" />

### mapear_fpga

Abre `/dev/mem` com syscall `open` (O_RDWR) e em seguida chama `mmap2` com os argumentos: endereço NULL, tamanho 4096, proteção PROT_READ|PROT_WRITE, flag MAP_SHARED, o file descriptor obtido, e o offset de página `0xFF200`. O endereço virtual mapeado é salvo na variável global `base_mmio` e também retornado em R0. Um ponto importante: R7 é o registrador de número de syscall e é callee-saved segundo a AAPCS — por isso `mapear_fpga` inclui R7 no seu `PUSH/POP`, garantindo que o valor do registrador seja preservado para o C.

### carregar_arquivo

Recebe em R0 o caminho do arquivo, em R1 o endereço do buffer de destino, e em R2 o tamanho da leitura. Antes de executar a syscall `open`, salva R2 em R6 por garantia para recuperar o valor caso o kernel modifique R2 durante a syscall. Após o `open`, salva o file descriptor em R4, chama `read` com o buffer e tamanho originais, e então chama `close`. Para garantir que dados importantes não sejam perdidos, todos os registradores R4–R8 e LR são salvos na pilha no início e restaurados ao final da rotina.

### enviar_instrucao e enviar_instrucao_sem_done

`enviar_instrucao` recebe em R0 a instrução de 32 bits, escreve no `data_in` e ativa o `enable`. A partir daí, entra em loop de polling no `data_out` testando o bit 4 (DONE) com a instrução `TST`. Quando `DONE = 1`, desativa o enable e retorna. `enviar_instrucao_sem_done` faz apenas a escrita e o pulso de enable, sem entrar no polling. É usada exclusivamente para o opcode 1 (STORE_WEIGHTS_ADDR), que não gera DONE.

### enviar_bias, enviar_beta, enviar_img

Cada função carrega `base_mmio` em R4, chama `carregar_arquivo` para ler o arquivo no buffer, e percorre o buffer em loop montando e enviando as instruções com `enviar_instrucao`:

- **bias**: Usa `LDRH` + `REV16` para carregar 2 bytes do valor do buffer e inverte os bytes para o formato little-endin, depois desloca os bits com `LSL #10` (valor nos bits [25:10]). Desloca o índice atual com `LSL #3` (índice nos bits [9:3]) e termina com o opcode 3 (STORE_BIAS).
- **beta**: Usa `LDRH` + `REV16` com o mesmo intuíto de carregar o valor do buffer, depois desloca os bits com `LSL #14` (valor nos bits [29:14]). Desloca o índice com `LSL #3` (índice nos bits [13:3]) e termina com o opcode 4 (STORE_BETA).
- **img**: Usa `LDRB` para carregar apenas 1 byte, e desloca os bits usando `LSL #13` (valor nos bits [20:13]). Desloca o índice atual com `LSL #3` (índice nos bits [12:3]) e termina com o opcode 0 (STORE_IMG).

Perceba que `enviar_img` só carrega 1 byte ao invés de 2 bytes como `enviar_bias` e `enviar_beta` (E assim como `enviar_peso`). A formatação dos valores da imagem em Q4.12 é feita dentro do coprocessador, então ela é enviada com a extensão normal de bits (8 bits por pixel).

### enviar_peso

Funciona de forma similar as outras rotinas de envio de dados, mas o envio dos pesos é o mais custoso. Os 100.352 valores precisam de um índice de 17 bits. Com o valor de 16 bits, opcode de 3 bits e a instrução de 32 bits, é inviável uma única instrução conter todos os operandos necessários. Portanto, para cada peso, são enviadas duas instruções:

1. **STORE_WEIGHTS_ADDR**: Envia apenas o índice nos bits [19:3] com o opcode 1, sem polling.
2. **STORE_WEIGHTS_VALUE**: Envia o valor nos bits [18:3] logo em seguida com o opcode 2, com polling de DONE

### iniciar_inferencia

Antes de disparar a inferência, aplica um pulso de `clr_operation` (bit 1 do `pio_signals`) para garantir que o flag DONE esteja em 0. Isso evita que o polling seguinte saia imediatamente ao encontrar um DONE stale de uma operação anterior. Em seguida, envia a instrução START (opcode 5), ativa o enable, e aguarda em polling até `DONE = 1`. Após a conclusão, lê `data_out`, desativa o enable, e aplica `AND R0, R0, #15` para isolar os 4 bits do dígito predito, que é retornado em R0 ao C para ser exibido no terminal e comparado com o valor esperado (Em caso de teste).

### reset_coprocessador

Escreve o valor `4` no `pio_signals` (bit 2, `rst = 1`), aguarda um delay de 0x5000 iterações com `SUBS/BNE` para garantir que o pulso dure ciclos suficientes para a FPGA registrar, e então escreve `0` para liberar o reset.

### Header driver.h

O `driver.h` é o contrato entre o C e o Assembly. Ele define as constantes do hardware (endereços, offsets, máscaras, opcodes, tamanhos de buffer) e declara as assinaturas das funções exportadas pelo assembly. Sem o header, o compilador C não saberia que as funções existem. Com ele, `enviar_bias("data/b_q.bin")` é compilado corretamente, com o ponteiro da string passado em R0 conforme a convenção AAPCS.

### Aplicação em C — main.c

O `main.c` é o orquestrador do sistema. Ele verifica se os arquivos existem no disco, chama `mapear_fpga` e `reset_coprocessador`, envia os parâmetros fixos da rede (`enviar_bias`, `enviar_beta` e `enviar_pesos`) uma única vez antes do loop do teste, e então itera sobre as 100 imagens: para cada uma, envia a imagem, dispara a inferência, registra o resultado e aplica um reset. Ao final, exibe a acurácia total. Os parâmetros da rede são enviados fora do loop porque não mudam entre imagens, reenviá-los a cada iteração desperdiçaria tempo enviando esses dados 100 vezes.

<div align="center">
<h1>

## Modo de Uso

</h1>
</div>

### Estrutura de Diretórios

<img width="300" height="500" alt="image" src="https://github.com/user-attachments/assets/5ec10c98-eb62-4ae2-aad3-456cbb4aaa6e" />


### Compilação

Na DE1-SoC (Linux ARM), C e assembly são compilados e linkados juntos em um único binário:

```bash
as -o driver.o driver.s
gcc -o main main.c driver.o
```

### Execução

O programa requer acesso ao `/dev/mem`, portanto deve ser executado como root:

```bash
sudo su
./main
```

<div align="center">
<h1>

## Problemas Encontrados e Correções

</h1>
</div>

**Offset incorreto no mmap2.** O endereço `0xFF200000` foi inicialmente passado diretamente como offset para o mmap2. A syscall espera o offset em unidades de páginas (4096 bytes), então o correto é `0xFF200000 / 4096 = 0xFF200`. Sem essa correção, o mapeamento apontava para uma região errada e todas as operações MMIO falhavam silenciosamente.

**R7 não salvo em mapear_fpga.** O registrador R7 é usado como número de syscall (open e mmap2) mas não estava sendo incluído no PUSH/POP da função. Como R7 é callee-saved pela convenção AAPCS, o C assumia que seu valor seria preservado após a chamada. Após mapear_fpga retornar com R7 corrompido, qualquer chamada de função subsequente no C poderia falhar de formas imprevisíveis. A correção foi incluir R7 no `PUSH {R4-R5, R7, LR}`.

**Interworking Thumb–ARM.** O GCC sem a flag `-marm` gera código Thumb-2 para o C, enquanto o assembly usa ARM (A32). Sem as diretivas corretas, o processador tentava executar ARM como Thumb ao entrar nas funções do driver, causando SIGTRAP ou Segmentation Fault. A correção foi adicionar `.syntax unified` e `.arm` no topo do arquivo assembly, e `.type funcname, %function` antes de cada função exportada, fazendo com que o linker gere os stubs de interworking automaticamente.

**R2 destruído pelo open() em carregar_arquivo.** O tamanho do buffer chegava em R2 e era usado depois pelo `read`. Porém, a syscall `open` pode modificar R2, perdendo o tamanho. A correção foi salvar R2 em R6 antes do open e restaurá-lo para R2 antes do read.

**Flag DONE stale causando saída prematura da espera de inferência.** Após o envio do último peso, a flag DONE ficava em 1. Quando `iniciar_inferencia` era chamada e o START era enviado, o loop de polling encontrava DONE = 1 (do peso anterior) imediatamente e retornava antes da inferência terminar, lendo um resultado inválido. A correção foi adicionar um pulso de `clr_operation` antes do START, garantindo DONE=0 antes de disparar a inferência.

**Segmentation Fault por relocation de variável global.** Uma versão intermediária tentou passar o endereço da ponte como variável global `.word 0` no assembly, armazenada por `mapear_fpga` e lida pelas demais funções via `LDR R4, =base_mmio; LDR R4, [R4]`. Problemas de relocation ao linkar o objeto assembly com o C faziam com que as funções lessem um endereço inválido ou zero. A solução foi manter a variável `base_mmio` no `.data` do assembly com as diretivas de interworking corretas.

<div align="center">
<h1>

## Resultados

</h1>
</div>

O sistema foi testado com 100 imagens do dataset MNIST, 10 por dígito, armazenadas nos arquivos `data/0.bin` a `data/99.bin`. Os resultados obtidos foram:

| Dígito | Acertos | Erros |
|--------|---------|-------|
| 0      | 8/10    | imagens 3 e 6 |
| 1      | 10/10   | — |
| 2      | 8/10    | imagens 26 e 27 |
| 3      | 9/10    | imagem 33 |
| 4      | 9/10    | imagem 40 |
| 5      | 6/10    | imagens 51, 53, 55, 59 |
| 6      | 7/10    | imagens 62, 64, 65 |
| 7      | 8/10    | imagens 72, 75 |
| 8      | 9/10    | imagem 80 |
| 9      | 9/10    | imagem 94 |

**Total: 83 acertos em 100 → Acurácia de 83%**

O dígito 1 obteve acurácia perfeita, enquanto o dígito 5 foi o mais difícil (60%), provavelmente por ser visualmente semelhante a 6 e 9. Para uma ELM com pesos fixos rodando em hardware dedicado em FPGA, 83% é um resultado satisfatório.

<div align="center">
<h1>

## Conclusão

</h1>
</div>

O driver desenvolvido neste marco cumpre o objetivo de estabelecer a comunicação entre o processador ARM e o coprocessador ELM na FPGA, realizando todo o fluxo de carregamento de parâmetros e inferência de forma integrada com a aplicação C.

A principal dificuldade do desenvolvimento foi lidar com os detalhes de baixo nível do Assembly ARMv7 em conjunto com as convenções do Linux: a ordem exata dos argumentos nas syscalls, a preservação dos registradores callee-saved, o offset correto para o mmap2 e o interworking entre os modos Thumb e ARM. Esses aspectos exigiram atenção constante durante a depuração, pois erros nessa camada geralmente não produzem mensagens de erro claras, o programa simplesmente trava ou produz resultados incorretos.

A experiência reforçou a compreensão prática da interface entre software e hardware em sistemas embarcados: desde a convenção de chamada AAPCS, passando pelo mapeamento de endereços físicos via MMIO, até o protocolo de handshake com o coprocessador e os detalhes de endianness dos dados. O sistema está funcional e validado com 83% de acurácia no dataset de teste.

<div align="center">
<h1>

## Referências

</h1>
</div>

PATTERSON, David A.; HENNESSY, John L. **Computer Organization and Design: The Hardware/Software Interface, ARM Edition**. Amsterdam: Morgan Kaufmann, 2017.

ARM LIMITED. **ARM Architecture Reference Manual — ARMv7-A and ARMv7-R edition**. Disponível em: https://developer.arm.com/documentation/ddi0406/latest

INTEL. **Cyclone V Hard Processor System Technical Reference Manual**. Disponível em: https://www.intel.com/content/www/us/en/docs/programmable/683126/current/overview.html

TECHNOLOGIES, Terasic. **DE1-SoC Board**. Disponível em: https://www.terasic.com.tw/cgi-bin/page/archive.pl?Language=English&No=836

THE LINUX KERNEL ORGANIZATION. **Linux Kernel Syscall Table for ARM**. Disponível em: https://syscalls.mebeim.net/?table=arm/32/eabi/latest

HUANG, Guang-Bin; ZHU, Qin-Yu; SIEW, Chee-Kheong. Extreme Learning Machine: Theory and Applications. **Neurocomputing**, v. 70, n. 1-3, p. 489-501, 2006.
