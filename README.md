<div align="center">
<h1>

## Sumário

</h1>
</div>

- [Introdução](#introdução)
- [Modificações do Driver](#modificações-do-driver)
- [Requisitos Principais](#requisitos-principais)
  - [Entrada e Saída](#entrada-e-saída)
  - [Os Três Modos de Operação](#os-três-modos-de-operação)
  - [Interface MMIO com o Controlador VGA](#interface-mmio-com-o-controlador-vga)
- [Fundamentação Teórica](#fundamentação-teórica)
  - [Integração do Controlador VGA](#integração-do-controlador-vga)
  - [Exibição da Imagem na Tela](#exibição-da-imagem-na-tela)
  - [Leitura do Mouse no Linux](#leitura-do-mouse-no-linux)
  - [Acesso MMIO pela Linguagem C](#acesso-mmio-pela-linguagem-c)
  - [Decodificação de PNG com stb_image](#decodificação-de-png-com-stb_image)
- [Metodologia](#metodologia)
- [Descrição da Solução](#descrição-da-solução)
  - [Arquitetura Geral da Aplicação](#arquitetura-geral-da-aplicação)
  - [Primitivas de Desenho no VGA](#primitivas-de-desenho-no-vga)
  - [Modo 1 — Inferência a Partir de Arquivo](#modo-1--inferência-a-partir-de-arquivo)
  - [Modo 2 — Desenho com o Mouse](#modo-2--desenho-com-o-mouse)
  - [Modo 3 — Benchmark](#modo-3--benchmark)
  - [Filtro de Blur](#filtro-de-blur)
  - [Inicialização e Menu](#inicialização-e-menu)
- [Modo de Uso](#modo-de-uso)
  - [Especificações do Ambiente](#especificações-do-ambiente)
  - [Configuração do Ambiente](#configuração-do-ambiente)
  - [Estrutura de Diretórios](#estrutura-de-diretórios)
  - [Compilação](#compilação)
  - [Execução](#execução)
- [Problemas Encontrados e Correções](#problemas-encontrados-e-correções)
- [Resultados](#resultados)
  - [Resultados Obtidos](#resultados-obtidos)
  - [Análise dos Resultados](#análise-dos-resultados)
- [Conclusão](#conclusão)
- [Referências](#referências)

---

<div align="center">
<h1>

## Introdução

</h1>
</div>

  Este documento descreve o desenvolvimento do Marco 3 de um sistema para classificação de dígitos numéricos, executado na placa DE1-SoC, um SoC que combina um processador ARM (HPS) com uma FPGA Cyclone V. Este é o marco final do projeto, no qual é desenvolvida a aplicação que o usuário de fato utiliza para interagir com o sistema, integrando os componentes desenvolvidos nos marcos anteriores: o coprocessador ELM em Verilog (Marco 1) e o driver em Assembly ARMv7 (Marco 2) com algumas alterações. Para a exibição das imagens, foi integrado o IP-Core VGA disponibilizado por Maike de Oliveira, cujo repositório original pode ser encontrado em: github.com/DestinyWolf/Problema_SD_2026_1. Caso também queira detalhes sobre o driver em Assembly feito no marco 2, seu repositório pode ser encontrado em: _github.com/San-Tana/Driver-classificador-de-imagens_. Porém, cabe o aviso de que o driver utilizado no marco 3 foi alterado, o que já será detalhado logo em seguida.

  O objetivo do Marco 3 é desenvolver uma aplicação em linguagem C que ofereça três modos de operação ao usuário: a classificação de uma imagem a partir de um arquivo, a classificação de um dígito desenhado na tela com o auxílio de um mouse, e um modo de benchmark que computa métricas de acurácia e desempenho. Todo o controle do controlador VGA e a leitura do mouse foram implementados diretamente na aplicação em C.

<div align="center">
<h1>

## Modificações do Driver

</h1>
</div>

Como dito acima, para o Marco 3, algumas mudanças foram feitas no código em Assembly. A principal mudança foi a remoção completa da leitura de arquivos no Assembly. No marco anterior, o arquivo `driver.s` precisava abrir e ler os dados usando chamadas de sistema. Agora, a função de abrir e ler os arquivos `.bin` e PNG foi transferida para a aplicação em C.

Por conta disso, as funções de envio do driver não recebem mais caminhos de texto. Elas agora recebem no registrador R0 o ponteiro exato da memória RAM onde o C já deixou os dados carregados. O Assembly apenas move esse endereço usando a instrução MOV R2, R0 e descarrega os dados sequencialmente na FPGA.

Por fim, ajustamos a forma como o C recebe o endereço da função `mapear_fpga`. Como o endereço de hardware da placa é muito alto (perto de 0xFF200000), o C podia achar que era um número negativo e errar a conversão. Usando um "casting" duplo, garantimos que os 32 bits do endereço cheguem inteiros para que a tela VGA funcione sem travar.

A versão atualizada do driver já está disponível neste mesmo repositório, então não há necessidade de fazer essa alteração por conta própria com o driver disponibilizado no repositório citado acima.

<div align="center">
<h1>

## Requisitos Principais

</h1>
</div>

### Entrada e Saída

A entrada do sistema é uma imagem de 28×28 pixels em escala de cinza, que pode vir de um arquivo PNG no primeiro modo, de um desenho feito pelo usuário com o mouse no segundo modo ou um arquivo em formato CSV, com uma lista de caminhos para arquivos PNG e os dígitos representados em cada arquivo, no terceiro modo. A saída é o dígito predito (0 a 9), impresso na interface em forma de texto, junto com a latência da inferência. No modo de benchmark, a saída também inclui as métricas calculadas e um arquivo de log em formato CSV.

Os parâmetros da rede neural continuam sendo lidos dos arquivos binários do diretório `data/`, da mesma forma que no Marco 2.

| Arquivo            | Conteúdo                                                     |
|--------------------|-------------------------------------------------------------|
| `data/w_in_q.bin`  | Pesos W_in em Q4.12 (int16)                                  |
| `data/b_q.bin`     | Bias em Q4.12 (int16)                                        |
| `data/beta_q.bin`  | Coeficientes beta em Q4.12 (int16)                          |
| `casoteste.csv`    | Lista de imagens do benchmark, no formato `caminho,esperado`|

### Os Três Modos de Operação

A aplicação apresenta um menu em modo texto com os três modos exigidos pelo enunciado:

| Modo | Descrição                                                              |
|------|------------------------------------------------------------------------|
| 1    | Inferência a partir de uma imagem PNG informada pelo usuário           |
| 2    | Inferência a partir de um dígito desenhado na tela com o mouse         |
| 3    | Validação/benchmark sobre um conjunto de imagens, com métricas e log   |

<img width="1250" height="546" alt="image" src="https://github.com/user-attachments/assets/b12027e9-8d0e-4072-b4e2-a21ff5549f64" />

### Interface MMIO com o Controlador VGA

Além dos três registradores do coprocessador ELM herdados do Marco 2 (offsets `0x00`, `0x10` e `0x20`), o sistema utiliza três novos registradores PIO do controlador VGA, adicionados ao projeto Quartus:

| Offset | Registrador       | Função                                                    |
|--------|-------------------|-----------------------------------------------------------|
| `0x30` | `pio_vga_status`  | Leitura do sinal done (bit 0)                             |
| `0x40` | `pio_vga_signals` | enable (bit 0), reset (bit 1)                             |
| `0x50` | `pio_data_in_vga` | Escrita da posição e cor: posy[28:19], posx[18:9], RGB[8:0] |

A cor é representada em 9 bits no formato RRRGGGBBB, ou seja, 3 bits por canal. O protocolo de escrita de um pixel segue o mesmo princípio de handshake do coprocessador: escreve-se o dado, pulsa-se o enable e aguarda-se o sinal de done.

<img width="824" height="183" alt="image" src="https://github.com/user-attachments/assets/a5bebe47-85d6-46b1-8247-c1228f2df5ef" />

<div align="center">
<h1>

## Fundamentação Teórica

</h1>
</div>

### Integração do Controlador VGA

Foi necessário adicionar ao projeto Quartus o IP-Core VGA (também referido neste documento como controlador VGA), que é um módulo Verilog instanciado na FPGA, ao lado do coprocessador ELM. Ele recebe a posição de um pixel e sua cor, escreve esse pixel em uma memória de vídeo interna, e essa memória alimenta a saída VGA física da placa, que é conectada a um monitor. A tela tem resolução de 320×240 pixels. Para a aplicação se comunicar com esse módulo, três PIOs foram adicionados ao projeto no Platform Designer do Quartus, nos offsets `0x30`, `0x40` e `0x50`. O controlador VGA não precisa estar dentro do coprocessador, basta apenas a correta instanciação dos PIOs para sua utilização.

### Exibição da Imagem na Tela

A imagem MNIST tem apenas 28×28 pixels, o que ficaria muito pequeno em uma tela de 320×240. Por isso, cada pixel da imagem é desenhado como um bloco de 8×8 pixels na tela, resultando em uma área de 224×224 pixels. Essa área é centralizada na tela, gerando uma margem de 48 pixels na horizontal (`(320 - 224) / 2`) e de 8 pixels na vertical (`(240 - 224) / 2`). Como a imagem está em escala de cinza de 8 bits e o controlador usa apenas 3 bits por canal, o valor de cada pixel é reduzido aos seus 3 bits mais significativos e replicado nos três canais de cor, produzindo um tom de cinza equivalente.

### Leitura do Mouse no Linux

No Linux, tudo é tratado como arquivo, então um mouse conectado por USB é mostrado pelo sistema como o arquivo `/dev/input/mice`, e seus movimentos e cliques podem ser lidos como um fluxo de bytes, da mesma forma que se lê um arquivo comum. A cada evento, o sistema fornece um pacote de três bytes: o primeiro contém o estado dos botões (bit 0 para o esquerdo, bit 1 para o direito), e os outros dois contêm os deslocamentos horizontal e vertical, ambos com sinal. É importante destacar que o mouse só passa deslocamentos relativos, ele não sabe o tamanho da tela nem onde está, então na aplicação a posição do cursor é incrementada por esses deslocamentos. Além disso, o mouse é lido apenas pelo processador ARM, a FPGA não interfere.

### Acesso MMIO pela Linguagem C

Diferente do Marco 2, onde deixamos a comunicação com o hardware no Assembly, neste marco passamos o controle da VGA direto para o código em C através de ponteiros. Para isso funcionar, o uso do `volatile` é fundamental. Como estamos lidando com registradores físicos da FPGA que mudam de estado sozinhos, o `volatile` impede que o compilador tente "otimizar" o código e ignore leituras repetidas (o que quebraria o nosso handshake de tela). A base para esses acessos vem direto do endereço virtual retornado por mapear_fpga.

### Decodificação de PNG com stb_image

Para o modo de inferência a partir de arquivo, é necessário ler imagens no formato PNG. Para isso, foi utilizada a biblioteca stb_image, que é de header único, ou seja, todo o seu código está contido em um único arquivo `.h`, sem necessidade de instalação. A biblioteca decodifica o PNG e, com o parâmetro adequado, força a saída para um único canal em escala de cinza, mesmo que a imagem original seja colorida. Após a leitura, a aplicação verifica se as dimensões são de fato 28×28 antes de prosseguir.

<div align="center">
<h1>

## Metodologia

</h1>
</div>

A metodologia usada no projeto foi a do PBL (Problem Based Learning), com reuniões em sessões tutoriais, onde a turma define metas e discute a solução do problema. As sessões de desenvolvimento foram fundamentais para evoluir no projeto, tirando dúvidas com o professor e os monitores. Durante as sessões tutoriais deste marco, foram debatidos tópicos como a integração do controlador VGA via PIOs, a melhor forma de exibir uma imagem pequena em uma tela maior, a leitura do mouse pelo sistema de arquivos do Linux, e estratégias para melhorar a precisão da inferência sobre desenhos feitos à mão.

A aplicação foi desenvolvida em linguagem C, com o driver Assembly do Marco 2 adaptado conforme descrito na seção "Modificações do Driver". Essa decisão respeita a separação de responsabilidades: o driver cuida exclusivamente da comunicação com o coprocessador ELM, enquanto a aplicação em C orquestra a leitura de arquivos, o controle do VGA, a leitura do mouse e a lógica dos três modos de operação.

<div align="center">
<h1>

## Descrição da Solução

</h1>
</div>

### Arquitetura Geral da Aplicação

A aplicação é organizada em torno de um menu interativo. Ao iniciar, ela executa uma rotina de inicialização que carrega os parâmetros da rede, mapeia a FPGA e envia os pesos, bias e beta ao coprocessador uma única vez. Em seguida, entra em um laço onde apresenta o menu e executa o modo escolhido pelo usuário. O acesso ao coprocessador é feito pelas funções do driver Assembly (`enviar_img`, `iniciar_inferencia`, etc.), enquanto o acesso ao controlador VGA é feito por funções em C escritas especificamente para este marco.

| Função              | Responsabilidade                                                  |
|---------------------|-------------------------------------------------------------------|
| `vga_write` / `vga_read` | Escreve e lê os registradores do VGA via ponteiro volatile   |
| `desenhar_pixel`    | Envia um pixel ao VGA seguindo o protocolo de handshake          |
| `desenhar_quadrado` | Preenche um bloco quadrado de pixels                             |
| `limpar_tela`       | Pinta toda a tela de preto                                       |
| `reset_vga`         | Aplica um pulso de reset no controlador VGA                      |
| `exibir_imagem`     | Exibe uma imagem 28×28 escalada na tela                         |
| `carregar_png`      | Lê uma imagem PNG do disco usando a stb_image                   |
| `aplicar_blur`      | Suaviza um desenho binário antes da inferência                  |
| `modo_arquivo`      | Implementa o modo de inferência a partir de arquivo             |
| `modo_desenho`      | Implementa o modo de desenho com o mouse                        |
| `modo_benchmark`    | Implementa o modo de validação com métricas                     |

### Primitivas de Desenho no VGA

A base de todo o desenho é a função `desenhar_pixel`. Ela monta a instrução de 32 bits empacotando a posição Y nos bits [28:19], a posição X nos bits [18:9] e a cor nos bits [8:0]. Em seguida, escreve essa instrução no registrador de dados (`0x50`), pulsa o sinal de enable (escreve 1 e depois 0 no `0x40`) e aguarda em polling o sinal de done (bit 0 do `0x30`) ficar em 1. A partir dessa primitiva, `desenhar_quadrado` preenche blocos com dois laços aninhados, `limpar_tela` percorre os 320×240 pixels da tela pintando tudo de preto, e `exibir_imagem` converte cada pixel da imagem MNIST em uma cor de cinza e o desenha como um bloco escalado.

### Modo 1 — Inferência a Partir de Arquivo

Neste modo, o usuário informa o caminho de uma imagem PNG. A aplicação lê a imagem com a stb_image, valida que ela tem 28×28 pixels, exibe-a na tela VGA e a envia ao coprocessador. A inferência é então disparada, e o dígito predito é impresso junto com a latência medida. A latência é medida usando a função `clock_gettime` com o relógio monotônico, que é imune a ajustes do horário do sistema, garantindo medições confiáveis.

<img width="480" height="514" alt="Modo-Arquivo" src="https://github.com/user-attachments/assets/01cc07d4-838f-4362-b7a6-23e99fe86f71" />

### Modo 2 — Desenho com o Mouse

A aplicação abre o dispositivo do mouse e entra em um laço lendo os pacotes de três bytes. A posição do cursor é mantida somando os deslocamentos, e é limitada à área de desenho de 224×224 pixels. Um cursor vermelho de 8×8 pixels acompanha o movimento: quando o cursor muda de célula, a célula anterior é restaurada à sua cor real e a nova é pintada de vermelho, dando a impressão de que o cursor se desloca pela tela.

Mantendo o botão esquerdo pressionado, o usuário pinta as células de branco. Para saber quais células foram pintadas, a aplicação mantém uma cópia do desenho em memória, chamada de shadow buffer. Esse buffer é necessário porque o controlador VGA só permite a escrita de pixels, não a leitura da memória de vídeo; sem ele, não haveria como recuperar o desenho para enviá-lo ao coprocessador. Ao pressionar o botão direito, o desenho é encerrado, e o conteúdo do shadow buffer (após o tratamento descrito adiante) é enviado para classificação. A saída pelo botão direito é detectada por transição, ou seja, o programa reage apenas ao momento em que o botão passa de solto para pressionado, evitando que o modo seja encerrado acidentalmente caso o botão já estivesse pressionado ao entrar.

<img width="886" height="475" alt="image" src="https://github.com/user-attachments/assets/4ce7fb0c-e1b9-464f-83da-8bd625b6d27a" />

### Modo 3 — Benchmark

No modo de validação, a aplicação lê um arquivo CSV de entrada onde cada linha contém o caminho de uma imagem PNG e o dígito esperado. Para cada imagem da lista, ela carrega o PNG, exibe a imagem na tela, mede o tempo da inferência e compara o resultado com o valor esperado. Ao final, calcula a acurácia, a latência média e seu desvio padrão, o tempo total e o throughput (imagens por segundo), exibindo essas métricas no terminal junto com a quantidade de imagens processadas, a quantidade de falhas de leitura e a quantidade de acertos, salvando um arquivo CSV de log com o resultado de cada imagem e o resumo final.

O desvio padrão calculado é o amostral, que divide a soma dos quadrados dos desvios por N menos 1, apropriado quando se trabalha com uma amostra. A latência de cada imagem mede apenas o tempo da inferência em si (envio da imagem e disparo), enquanto o tempo total, usado no cálculo do throughput, abrange todo o laço, incluindo a exibição na tela. A escolha de ler as imagens a partir de um CSV de entrada torna o modo flexível: para testar um conjunto diferente, basta trocar o arquivo de entrada, sem necessidade de recompilar o programa.

A automação dos testes está integrada à própria aplicação, dentro do `modo_benchmark`. Basta listar o caminho das imagens e o dígito representado nela em um arquivo CSV e selecionar a opção 3 do menu, e a aplicação roda todo o conjunto, calcula as métricas e gera o CSV de log automaticamente. Por padrão, a aplicação irá procurar o arquivo `casoteste.csv` no diretório atual. Para ajudar, também disponibilizamos um arquivo CSV já preenchido com 100 imagens, 10 de cada digito, as quais estão disponíveis na pasta `test`.

<img width="673" height="187" alt="image" src="https://github.com/user-attachments/assets/7054c5ef-662f-4010-afbe-4b2a067d1f31" />

### Filtro de Blur

Durante os testes do modo de desenho, observamos que a maioria das inferências estava errada. Investigando, identificamos que o problema vinha da diferença entre o que era desenhado e as imagens com que a rede havia sido construída. As imagens originais do MNIST têm bordas suaves, com tons de cinza nas transições, enquanto o nosso desenho era puramente binário, com pixels totalmente brancos ou totalmente pretos. Para aproximar o desenho do estilo esperado pela rede, foi adicionado um filtro de blur 3×3, que substitui cada pixel pela média de seus vizinhos válidos, criando as transições de cinza nas bordas. Esse filtro é aplicado apenas no momento de enviar o desenho ao coprocessador. Visualmente, a tela continua mostrando os pixels sem o efeito do blur. A aplicação do blur melhorou um pouco a acurácia da inferência, mas ainda aconteciam erros.

### Inicialização e Menu

A rotina de inicialização é executada uma única vez ao abrir o programa. Ela carrega os arquivos de pesos, bias e beta, mapeia a ponte HPS-FPGA, reseta o coprocessador e o controlador VGA, e envia os parâmetros da rede ao coprocessador. O envio dos parâmetros é feito apenas nessa etapa porque eles não mudam entre as inferências, reenviá-los a cada classificação desperdiçaria um tempo considerável, já que apenas os pesos somam mais de cem mil valores. Após a inicialização, o programa entra no laço do menu, onde o usuário escolhe entre os três modos ou encerra a aplicação.

<div align="center">
<h1>

## Modo de Uso

</h1>
</div>

### Especificações do Ambiente

O sistema foi desenvolvido e validado usando as seguintes ferramentas:

| Componente / Software | Função no Projeto |
|-----------------------|-------------------|
| Intel Quartus Prime Lite | Síntese da FPGA e instanciação dos PIOs |
| Linux embarcado na DE1-SoC | Sistema operacional no ARM HPS |
| GCC (toolchain ARM) | Compilação do código C |
| GNU Assembler (as) | Montagem do driver Assembly |
| Biblioteca stb_image | Decodificação de PNG |

### Configuração do Ambiente

Antes de compilar e executar a aplicação, alguns passos de preparação são necessários na placa DE1-SoC:

1. **Hardware conectado.** O monitor deve estar ligado à saída VGA da placa, e o mouse USB conectado a uma das portas USB da placa antes de inicializar o Linux embarcado. A inicialização do sistema reconhece o mouse e o expõe automaticamente em `/dev/input/mice`.

2. **Projeto Quartus Carregado (FPGA).** O projeto Quartus, com o coprocessador ELM e os PIOs do controlador VGA mapeados nos offsets `0x30`, `0x40` e `0x50`, precisa estar carregado na FPGA. Sem isso, os acessos MMIO do C não chegam ao hardware correto.

3. **Arquivos no diretório de execução.** Os pesos da rede (`w_in_q.bin`, `b_q.bin`, `beta_q.bin`) devem estar dentro de uma pasta `data/` no mesmo diretório do executável. O arquivo `casoteste.csv` e a pasta `test/` com as imagens PNG do benchmark devem estar na raiz do projeto.

4. **Acesso a /dev/mem.** A aplicação precisa abrir `/dev/mem` para mapear a ponte HPS-FPGA, o que exige privilégios de root. Por isso, a execução deve ser feita com `sudo` ou em um shell aberto com `sudo su`.

### Estrutura de Diretórios

```
projeto/
├── main.c            (aplicação em C com os três modos)
├── driver.s          (driver Assembly, com algumas alterações em relação ao marco 2)
├── driver.h          (constantes e protótipos das funções do driver)
├── stb_image.h       (biblioteca para leitura de PNG)
├── casoteste.csv     (lista de imagens do benchmark)
├── test/             (imagens PNG de teste, organizadas por dígito)
└── data/
    ├── w_in_q.bin
    ├── b_q.bin
    └── beta_q.bin
```

### Compilação

Na DE1-SoC (Linux ARM), o assembly é montado como objeto e linkado com o C pelo GCC:

```bash
as -o driver.o driver.s
gcc -o main main.c driver.o -lm -lrt -std=gnu99
```

A flag `-lm` liga a biblioteca math, necessária para a função `sqrt` usada no cálculo do desvio padrão. A flag `-lrt` liga a biblioteca de tempo real, onde fica a função `clock_gettime`. A flag `-std=gnu99` permite declarar variáveis dentro dos laços `for` e, ao mesmo tempo, mantém habilitadas as extensões POSIX necessárias para a medição de tempo, reforçadas pela diretiva `_POSIX_C_SOURCE` no topo do `main.c`.

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

**Tela preta intermitente no controlador VGA.** Em algumas execuções, a tela funcionava normalmente, mas em outras ficava completamente preta. Identificamos que o controlador VGA podia iniciar em um estado interno inconsistente, dependendo de como havia terminado a execução anterior. A primeira versão da rotina de reset usava um pulso curto, com cerca de 0x5000 iterações de delay, insuficiente para a máquina de estados do controlador estabilizar em todos os casos. A correção foi reforçar essa rotina: primeiro zerando os sinais para garantir um estado conhecido, depois aplicando o pulso de reset e, por fim, aumentando o tempo de espera para 0x10000 iterações em cada etapa. Após esse ajuste, a tela passou a aparecer sempre que o sistema era inicializado.

**Inferências incorretas no modo de desenho.** Ao testar o modo de desenho, percebemos que a maioria dos dígitos era classificada de forma errada. A causa era a diferença entre o desenho feito com o mouse, e as imagens originais do MNIST, que possuem bordas suaves. A solução foi aplicar um filtro de blur 3×3 sobre o desenho antes de enviá-lo ao coprocessador, criando as transições de cinza que a rede esperava encontrar. Essa mudança melhorou a taxa de acerto sobre os desenhos manuais.

**Encerramento imediato do modo de desenho.** Em uma versão inicial, ao entrar no modo de desenho, ele era encerrado instantaneamente. Isso acontecia porque o botão direito ainda estava registrado como pressionado de uma ação anterior, e o código reagia ao nível do botão em vez da sua transição. A correção foi consumir os eventos residuais ao entrar no modo e passar a detectar a transição de solto para pressionado, garantindo que o modo só encerre quando o usuário de fato clicar o botão direito naquele momento.

**clock_gettime indefinido na compilação.** Ao adicionar a medição de tempo, o linker reclamava que a função `clock_gettime` não estava definida. Isso ocorria porque, nas versões antigas de glibc da placa, essa função reside em uma biblioteca separada de tempo real. A correção foi adicionar a flag `-lrt` ao comando de compilação, além de manter a diretiva `_POSIX_C_SOURCE` no topo do arquivo para habilitar as extensões POSIX.

<div align="center">
<h1>

## Resultados

</h1>
</div>

### Resultados Obtidos

O sistema foi validado nos três modos de operação. No modo de arquivo, imagens conhecidas do dataset foram classificadas corretamente e exibidas na tela. No modo de desenho, foi possível desenhar dígitos com o mouse e obter predições, com a melhora de precisão proporcionada pelo filtro de blur. No modo de benchmark, conjuntos de imagens listados em CSV foram processados automaticamente, gerando as métricas e o log.

Os testes de desempenho mostraram uma latência de inferência bastante estável, em torno de 18 ms por imagem, com desvio padrão praticamente nulo. O throughput observado ficou em torno de 9 a 10 imagens por segundo. Quanto à acurácia, com imagens do dataset o sistema reproduz o resultado obtido no Marco 2, em torno de 83%.

### Análise dos Resultados

A latência é estável porque o coprocessador executa sempre a mesma sequência de operações para qualquer imagem, independentemente do conteúdo, então o tempo de cada inferência tende a ser idêntico. Isso fica claro pelo desvio padrão praticamente nulo no benchmark.

O throughput, por outro lado, é dominado pelo tempo de exibição da imagem na tela VGA, e não pela inferência em si. Como cada pixel exigiria atravessar a ponte HPS-FPGA, desenhar uma imagem inteira escalada 8× leva mais tempo do que classificá-la. Isso explica por que o throughput total fica em ~9 img/s, mesmo com a inferência levando apenas 18 ms.

A acurácia de 83% no dataset é consistente com o resultado do Marco 2, o que era esperado, já que a rede e seus pesos não mudaram. No modo de desenho, a acurácia é menor e mais variável: um traço feito à mão com o mouse dificilmente reproduz a distribuição de tons e a centralização das imagens originais do MNIST, e o filtro de blur, embora ajude, não elimina essa diferença.

<div align="center">
<h1>

## Conclusão

</h1>
</div>

Ao final, a aplicação entrega o sistema completo, integrando o coprocessador, o driver e o controlador VGA em um programa único com os três modos de operação pedidos. O usuário pode classificar uma imagem de arquivo, desenhar um dígito com o mouse e rodar um conjunto de validação que reporta acurácia, latência, desvio padrão e throughput, salvando os resultados em CSV.

O principal gargalo de desempenho encontrado está na exibição da imagem no monitor. Cada pixel desenhado exige uma escrita, um pulso de enable e uma espera pelo sinal de done, e cada um desses acessos atravessa a ponte HPS-FPGA, que tem uma latência considerável. Como a imagem é exibida em escala 8×, são desenhados mais de cinquenta mil pixels por imagem, o que se sobressai no tempo total. 

Das melhorias que tentamos, duas funcionaram bem: o reforço da rotina de reset do VGA, que resolveu o problema da tela preta, e a aplicação do filtro de blur no modo de desenho, que melhorou a precisão sobre os traços manuais. Ainda assim, a entrada manual segue sendo a maior fonte de imprecisão do sistema, e melhorá-la seria o próximo passo natural se o projeto fosse evoluído.

Por fim, é importante mencionar a metodologia PBL ao longo de todo o projeto. A construção do sistema em marcos sucessivos, partindo do coprocessador em hardware, passando pelo driver em Assembly e chegando à aplicação em C, ajudou a entender na prática como as diferentes camadas de um sistema embarcado se conectam, da lógica digital na FPGA até a interface com o usuário no Linux.

<div align="center">
<h1>

## Referências

</h1>
</div>

PATTERSON, David A.; HENNESSY, John L. **Computer Organization and Design: The Hardware/Software Interface, ARM Edition**. Amsterdam: Morgan Kaufmann, 2017.

INTEL. **Cyclone V Hard Processor System Technical Reference Manual**. Disponível em: https://www.intel.com/content/www/us/en/docs/programmable/683126/current/overview.html

TECHNOLOGIES, Terasic. **DE1-SoC Board**. Disponível em: https://www.terasic.com.tw/cgi-bin/page/archive.pl?Language=English&No=836

THE LINUX KERNEL ORGANIZATION. **Linux Kernel Syscall Table for ARM**. Disponível em: https://syscalls.mebeim.net/?table=arm/32/eabi/latest

BARRETT, Sean. **stb_image — Public domain image loader**. Disponível em: https://github.com/nothings/stb

HUANG, Guang-Bin; ZHU, Qin-Yu; SIEW, Chee-Kheong. Extreme Learning Machine: Theory and Applications. **Neurocomputing**, v. 70, n. 1-3, p. 489-501, 2006.
