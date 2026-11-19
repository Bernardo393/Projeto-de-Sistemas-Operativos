#include "bolsa.h"
#include "comunicacao.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static ListaCarteiras lista_carteiras;
static volatile sig_atomic_t em_execucao = 1;

static void terminar_strings_pedido(PedidoInvestidor *pedido) {
    pedido->id_corretora[MAX_NOME - 1] = '\0';
    pedido->id_investidor[MAX_NOME - 1] = '\0';
    pedido->fifo_resposta[MAX_PATH - 1] = '\0';
    pedido->ordem.corretora[MAX_NOME - 1] = '\0';
    pedido->ordem.investidor[MAX_NOME - 1] = '\0';
    pedido->ordem.nome_empresa[MAX_NOME - 1] = '\0';
}

static void terminar_strings_mensagem(MensagemBolsaValores *mensagem) {
    mensagem->id_corretora[MAX_NOME - 1] = '\0';
    mensagem->mensagem[sizeof(mensagem->mensagem) - 1] = '\0';

    if (mensagem->tipo == MSG_NOTIFICACAO_TRANSACAO) {
        mensagem->dados.notificacao.corretora[MAX_NOME - 1] = '\0';
        mensagem->dados.notificacao.investidor[MAX_NOME - 1] = '\0';
        mensagem->dados.notificacao.nome_empresa[MAX_NOME - 1] = '\0';
    }
}

static int identificador_valido(const char *identificador) {
    if (identificador[0] == '\0' ||
        strcmp(identificador, ".") == 0 ||
        strcmp(identificador, "..") == 0) {
        return 0;
    }

    for (size_t i = 0; identificador[i] != '\0'; i++) {
        if (identificador[i] == '/' ||
            identificador[i] == '\\' ||
            isspace((unsigned char) identificador[i])) {
            return 0;
        }
    }

    return 1;
}

static void tratar_sinal_encerramento(int sinal) {
    (void) sinal;
    em_execucao = 0;
}

static void configurar_sinais(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_sinal_encerramento;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Sem SA_RESTART para interromper o poll() imediatamente

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void inicializar_corretora(void) {
    memset(&lista_carteiras, 0, sizeof(lista_carteiras));

    if (pthread_mutex_init(&lista_carteiras.mutex, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar o mutex das carteiras.\n");
        exit(EXIT_FAILURE);
    }
}

static int criar_ou_validar_carteira(const char *id_investidor) {
    pthread_mutex_lock(&lista_carteiras.mutex);

    for (int i = 0; i < lista_carteiras.num_carteiras; i++) {
        if (strcmp(lista_carteiras.carteiras[i].id_investidor, id_investidor) == 0) {
            pthread_mutex_unlock(&lista_carteiras.mutex);
            return 1; // Carteira já existente
        }
    }

    if (lista_carteiras.num_carteiras >= MAX_INVESTIDORES) {
        pthread_mutex_unlock(&lista_carteiras.mutex);
        return 0; // Limite atingido
    }

    CarteiraInvestidor *nova = &lista_carteiras.carteiras[lista_carteiras.num_carteiras];
    memset(nova, 0, sizeof(*nova));

    if (!copiar_texto(nova->id_investidor, sizeof(nova->id_investidor), id_investidor)) {
        pthread_mutex_unlock(&lista_carteiras.mutex);
        return 0;
    }

    lista_carteiras.num_carteiras++;
    pthread_mutex_unlock(&lista_carteiras.mutex);
    return 1;
}

static int copiar_carteira(const char *id_investidor, CarteiraInvestidor *destino) {
    pthread_mutex_lock(&lista_carteiras.mutex);

    for (int i = 0; i < lista_carteiras.num_carteiras; i++) {
        if (strcmp(lista_carteiras.carteiras[i].id_investidor, id_investidor) == 0) {
            *destino = lista_carteiras.carteiras[i]; // Cópia segura dentro da secção crítica
            pthread_mutex_unlock(&lista_carteiras.mutex);
            return 1;
        }
    }

    pthread_mutex_unlock(&lista_carteiras.mutex);
    return 0;
}

static void registar_ordem_pendente_local(
    const char *id_investidor,
    const OrdemCV *ordem,
    const char *log_corretora
) {
    pthread_mutex_lock(&lista_carteiras.mutex);

    for (int i = 0; i < lista_carteiras.num_carteiras; i++) {
        CarteiraInvestidor *carteira = &lista_carteiras.carteiras[i];
        if (strcmp(carteira->id_investidor, id_investidor) == 0) {
            if (carteira->num_ordens_pendentes < MAX_HISTORICO) {
                carteira->ordens_pendentes[carteira->num_ordens_pendentes] = *ordem;
                carteira->num_ordens_pendentes++;
            } else {
                gravar_log(
                    log_corretora,
                    "Aviso: limite de ordens pendentes atingido para o investidor."
                );
            }
            break;
        }
    }

    pthread_mutex_unlock(&lista_carteiras.mutex);
}

static int criar_fifo_publico_seguro(const char *fifo_publico) {
    if (mkfifo(fifo_publico, 0666) == 0) {
        return 1;
    }

    if (errno != EEXIST) {
        perror("Corretora: erro ao criar FIFO público");
        return 0;
    }

    // Verificar se outra corretora está ativamente a escutar este FIFO
    int fd_teste = open(fifo_publico, O_WRONLY | O_NONBLOCK);
    if (fd_teste >= 0) {
        close(fd_teste);
        fprintf(stderr, "Erro: já existe uma corretora ativa com este identificador (%s).\n", fifo_publico);
        return 0;
    }

    if (errno != ENXIO) {
        perror("Corretora: erro ao validar FIFO existente");
        return 0;
    }

    // FIFO órfão de uma execução terminada abruptamente: seguro remover e recriar
    if (unlink(fifo_publico) < 0) {
        perror("Corretora: erro ao remover FIFO órfão");
        return 0;
    }

    if (mkfifo(fifo_publico, 0666) < 0) {
        perror("Corretora: erro ao recriar FIFO público");
        return 0;
    }

    return 1;
}

static int ligar_bolsa_valores(const char *id_corretora, const char *ip_bv, const char *log_corretora) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Corretora: erro ao criar socket TCP");
        gravar_log(log_corretora, "Erro: falha ao criar socket TCP.");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORTA_BV);

    if (inet_pton(AF_INET, ip_bv, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Corretora: endereço IP inválido '%s'.\n", ip_bv);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Corretora: falha ao ligar à Bolsa de Valores");
        gravar_log(log_corretora, "Erro: falha ao estabelecer ligação TCP com a BV.");
        close(sock);
        return -1;
    }

    MensagemBolsaValores registo;
    memset(&registo, 0, sizeof(registo));
    registo.tipo = MSG_REGISTAR_CORRETORA;
    copiar_texto(registo.id_corretora, sizeof(registo.id_corretora), id_corretora);

    if (!escrever_todos(sock, &registo, sizeof(registo))) {
        fprintf(stderr, "Corretora: erro ao enviar pedido de registo à BV.\n");
        gravar_log(log_corretora, "Erro: falha ao enviar registo à BV.");
        close(sock);
        return -1;
    }

    MensagemBolsaValores resposta_registo;
    memset(&resposta_registo, 0, sizeof(resposta_registo));

    if (!ler_todos(sock, &resposta_registo, sizeof(resposta_registo))) {
        fprintf(stderr, "Corretora: a BV encerrou a ligação durante o registo.\n");
        gravar_log(log_corretora, "Erro: sem resposta da BV ao registo.");
        close(sock);
        return -1;
    }
    terminar_strings_mensagem(&resposta_registo);

    if (resposta_registo.tipo != MSG_RESPOSTA_OK) {
        fprintf(stderr, "Corretora: registo rejeitado pela BV: %s\n", resposta_registo.mensagem);
        gravar_log(log_corretora, resposta_registo.mensagem);
        close(sock);
        return -1;
    }

    printf("Registada na Bolsa de Valores (%s:%d): %s\n", ip_bv, PORTA_BV, resposta_registo.mensagem);
    gravar_log(log_corretora, "Registo na Bolsa de Valores concluído com sucesso.");
    return sock;
}

static void registar_notificacao_bv(
    const MensagemBolsaValores *mensagem,
    const char *log_corretora
) {
    char mensagem_log[256];

    snprintf(
        mensagem_log,
        sizeof(mensagem_log),
        "Notificação recebida da BV para '%s': %d ações de '%s' a %.2f.",
        mensagem->dados.notificacao.investidor,
        mensagem->dados.notificacao.quantidade,
        mensagem->dados.notificacao.nome_empresa,
        mensagem->dados.notificacao.preco_definitivo
    );
    gravar_log(log_corretora, mensagem_log);
}

static int comunicar_com_bv(
    int socket_bv,
    const MensagemBolsaValores *pedido,
    MensagemBolsaValores *resposta,
    const char *log_corretora
) {
    if (!escrever_todos(socket_bv, pedido, sizeof(*pedido))) {
        return 0;
    }

    while (1) {
        memset(resposta, 0, sizeof(*resposta));

        if (!ler_todos(socket_bv, resposta, sizeof(*resposta))) {
            return 0;
        }
        terminar_strings_mensagem(resposta);

        if (resposta->tipo == MSG_NOTIFICACAO_TRANSACAO) {
            registar_notificacao_bv(resposta, log_corretora);
            continue;
        }

        if (resposta->tipo != MSG_RESPOSTA_OK &&
            resposta->tipo != MSG_RESPOSTA_ERRO) {
            gravar_log(
                log_corretora,
                "Erro: tipo de resposta inesperado recebido da BV."
            );
            return 0;
        }

        return 1;
    }
}

static int enviar_resposta(const PedidoInvestidor *pedido, const RespostaCorretora *resposta) {
    int fd_resposta = open(
        pedido->fifo_resposta,
        O_WRONLY | O_NONBLOCK
    );

    if (fd_resposta < 0) {
        if (errno == ENXIO || errno == ENOENT) {
            fprintf(
                stderr,
                "Corretora: investidor '%s' já encerrou antes da resposta.\n",
                pedido->id_investidor
            );
        } else {
            perror("Corretora: erro ao abrir FIFO privado do investidor");
        }
        return 0;
    }

    int flags = fcntl(fd_resposta, F_GETFL, 0);
    if (flags < 0 ||
        fcntl(fd_resposta, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        perror("Corretora: erro ao configurar FIFO privado do investidor");
        close(fd_resposta);
        return 0;
    }

    int sucesso = escrever_todos(fd_resposta, resposta, sizeof(*resposta));
    close(fd_resposta);
    return sucesso;
}

static void preparar_resposta_base(
    const PedidoInvestidor *pedido,
    const char *id_corretora,
    RespostaCorretora *resposta
) {
    memset(resposta, 0, sizeof(*resposta));
    resposta->tipo = MSG_RESPOSTA_OK;
    copiar_texto(resposta->id_corretora, sizeof(resposta->id_corretora), id_corretora);
    copiar_texto(resposta->id_investidor, sizeof(resposta->id_investidor), pedido->id_investidor);
}

static void processar_pedido(
    const PedidoInvestidor *pedido,
    const char *id_corretora,
    int socket_bv,
    const char *log_corretora,
    int *bv_ativa
) {
    RespostaCorretora resposta;
    preparar_resposta_base(pedido, id_corretora, &resposta);

    switch (pedido->tipo) {
        case MSG_LOGIN_INVESTIDOR:
            if (!criar_ou_validar_carteira(pedido->id_investidor)) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Limite máximo de investidores atingido na corretora.");
            } else {
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Login efetuado com sucesso. Carteira ativa.");
            }
            break;

        case MSG_CONSULTAR_CARTEIRA:
            if (!copiar_carteira(pedido->id_investidor, &resposta.dados.carteira)) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Carteira do investidor não encontrada.");
            } else {
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Carteira consultada com sucesso.");
            }
            break;

        case MSG_CONSULTAR_ORDENS:
            if (!copiar_carteira(pedido->id_investidor, &resposta.dados.carteira)) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Carteira do investidor não encontrada.");
            } else {
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Histórico de ordens e transações consultado com sucesso.");
            }
            break;

        case MSG_LISTAR_COTACOES: {
            MensagemBolsaValores pedido_bv;
            MensagemBolsaValores resp_bv;

            memset(&pedido_bv, 0, sizeof(pedido_bv));
            pedido_bv.tipo = MSG_LISTAR_COTACOES;
            copiar_texto(pedido_bv.id_corretora, sizeof(pedido_bv.id_corretora), id_corretora);

            if (!comunicar_com_bv(
                    socket_bv,
                    &pedido_bv,
                    &resp_bv,
                    log_corretora
                )) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Ligação à Bolsa de Valores perdida.");
                gravar_log(log_corretora, "Erro: ligação TCP à BV perdida ao listar cotações.");
                *bv_ativa = 0;
                em_execucao = 0;
            } else {
                resposta.tipo = resp_bv.tipo;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem), resp_bv.mensagem);

                if (resp_bv.tipo == MSG_RESPOSTA_OK &&
                    resp_bv.num_elementos >= 0 &&
                    resp_bv.num_elementos <= MAX_EMPRESAS) {
                    for (int i = 0; i < resp_bv.num_elementos; i++) {
                        resp_bv.dados.cotacoes[i].nome[MAX_NOME - 1] = '\0';
                    }
                    resposta.num_elementos = resp_bv.num_elementos;
                    memcpy(resposta.dados.cotacoes, resp_bv.dados.cotacoes,
                           (size_t) resp_bv.num_elementos * sizeof(Empresa));
                } else if (resp_bv.tipo == MSG_RESPOSTA_OK) {
                    resposta.tipo = MSG_RESPOSTA_ERRO;
                    resposta.num_elementos = 0;
                    copiar_texto(
                        resposta.mensagem,
                        sizeof(resposta.mensagem),
                        "Resposta inválida da Bolsa de Valores."
                    );
                    gravar_log(
                        log_corretora,
                        "Erro: a BV devolveu um número inválido de cotações."
                    );
                }
            }
            break;
        }

        case MSG_EMITIR_ORDEM: {
            CarteiraInvestidor temp;
            if (!copiar_carteira(pedido->id_investidor, &temp)) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Carteira do investidor não encontrada.");
                break;
            }

            MensagemBolsaValores pedido_bv;
            MensagemBolsaValores resp_bv;

            memset(&pedido_bv, 0, sizeof(pedido_bv));
            pedido_bv.tipo = MSG_EMITIR_ORDEM;
            copiar_texto(pedido_bv.id_corretora, sizeof(pedido_bv.id_corretora), id_corretora);
            pedido_bv.dados.ordem = pedido->ordem;
            // Garantir que os identificadores na ordem coincidem com a sessão autenticada
            copiar_texto(pedido_bv.dados.ordem.corretora, sizeof(pedido_bv.dados.ordem.corretora), id_corretora);
            copiar_texto(pedido_bv.dados.ordem.investidor, sizeof(pedido_bv.dados.ordem.investidor), pedido->id_investidor);

            if (!comunicar_com_bv(
                    socket_bv,
                    &pedido_bv,
                    &resp_bv,
                    log_corretora
                )) {
                resposta.tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                             "Ligação à Bolsa de Valores perdida ao submeter ordem.");
                gravar_log(log_corretora, "Erro: ligação TCP à BV perdida ao emitir ordem.");
                *bv_ativa = 0;
                em_execucao = 0;
            } else {
                resposta.tipo = resp_bv.tipo;
                copiar_texto(resposta.mensagem, sizeof(resposta.mensagem), resp_bv.mensagem);

                if (resp_bv.tipo == MSG_RESPOSTA_OK) {
                    registar_ordem_pendente_local(
                        pedido->id_investidor,
                        &pedido_bv.dados.ordem,
                        log_corretora
                    );
                }
            }
            break;
        }

        case MSG_TERMINAR_SESSAO:
            printf("[Corretora] Investidor '%s' terminou a sessão.\n", pedido->id_investidor);
            copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                         "Sessão encerrada de forma ordeira.");
            break;

        default:
            resposta.tipo = MSG_RESPOSTA_ERRO;
            copiar_texto(resposta.mensagem, sizeof(resposta.mensagem),
                         "Tipo de mensagem desconhecido.");
            break;
    }

    if (!enviar_resposta(pedido, &resposta)) {
        fprintf(stderr, "Corretora: falha ao enviar resposta para '%s'.\n", pedido->id_investidor);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Uso correto: %s <id_corretora> [ip_bv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    configurar_sinais();

    char id_corretora[MAX_NOME];
    const char *ip_bv = (argc == 3) ? argv[2] : IP_BV;

    if (!copiar_texto(id_corretora, sizeof(id_corretora), argv[1])) {
        fprintf(stderr, "Erro: identificador da corretora demasiado comprido.\n");
        return EXIT_FAILURE;
    }
    if (!identificador_valido(id_corretora)) {
        fprintf(stderr, "Erro: identificador da corretora inválido.\n");
        return EXIT_FAILURE;
    }

    char log_corretora[128];
    int tamanho = snprintf(log_corretora, sizeof(log_corretora), "eventos_corretora_%s.txt", id_corretora);
    if (tamanho < 0 || (size_t) tamanho >= sizeof(log_corretora)) {
        fprintf(stderr, "Erro: nome do ficheiro de log demasiado comprido.\n");
        return EXIT_FAILURE;
    }

    char mensagem_log[200];
    snprintf(mensagem_log, sizeof(mensagem_log), "=== ARRANQUE DA CORRETORA: %s ===", id_corretora);
    gravar_log(log_corretora, mensagem_log);

    inicializar_corretora();

    // 1. Estabelecer ligação TCP e registar na Bolsa de Valores (Secção 3.2.3)
    int socket_bv = ligar_bolsa_valores(id_corretora, ip_bv, log_corretora);
    if (socket_bv < 0) {
        fprintf(stderr, "Erro: não foi possível registar na BV. Inicie primeiro o servidor_bolsa.\n");
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    // 2. Criar e abrir o FIFO público de forma segura
    char fifo_publico[MAX_PATH];
    tamanho = snprintf(fifo_publico, sizeof(fifo_publico), "%s%s", FIFO_CORRETORA_PREFIX, id_corretora);
    if (tamanho < 0 || (size_t) tamanho >= sizeof(fifo_publico)) {
        fprintf(stderr, "Erro: caminho do FIFO público demasiado longo.\n");
        close(socket_bv);
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    if (!criar_fifo_publico_seguro(fifo_publico)) {
        gravar_log(log_corretora, "Erro: não foi possível criar o FIFO público.");
        close(socket_bv);
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    // O_RDWR mantém o FIFO sempre aberto para leitura/escrita, evitando EOFs falsos e ENXIO
    int fd_fifo = open(fifo_publico, O_RDWR);
    if (fd_fifo < 0) {
        perror("Corretora: erro ao abrir FIFO público");
        unlink(fifo_publico);
        close(socket_bv);
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    printf("Corretora '%s' online.\nA aguardar investidores em: %s\n", id_corretora, fifo_publico);
    snprintf(mensagem_log, sizeof(mensagem_log), "FIFO público ativo: %s", fifo_publico);
    gravar_log(log_corretora, mensagem_log);

    // 3. Ciclo principal de multiplexagem (FIFO local + estado do socket da BV)
    struct pollfd fds[2];
    int bv_ativa = 1;
    fds[0].fd = fd_fifo;
    fds[0].events = POLLIN;
    fds[1].fd = socket_bv;
    fds[1].events = POLLIN;

    while (em_execucao) {
        int prontos = poll(fds, 2, -1);
        if (prontos < 0) {
            if (errno == EINTR) {
                continue; // Reavalia em_execucao após SIGINT/SIGTERM
            }
            perror("Corretora: erro no poll");
            break;
        }

        // Verificar se a Bolsa de Valores encerrou ou enviou notificação assíncrona
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            MensagemBolsaValores msg_bv;
            if (!ler_todos(socket_bv, &msg_bv, sizeof(msg_bv))) {
                printf("\n[Corretora] A Bolsa de Valores encerrou a ligação. A terminar...\n");
                gravar_log(log_corretora, "Ligação encerrada pela Bolsa de Valores.");
                bv_ativa = 0;
                break;
            }
            terminar_strings_mensagem(&msg_bv);

            if (msg_bv.tipo == MSG_NOTIFICACAO_TRANSACAO) {
                registar_notificacao_bv(&msg_bv, log_corretora);
            }
        }

        // Processar pedido local de um investidor
        if (fds[0].revents & POLLIN) {
            PedidoInvestidor pedido;
            memset(&pedido, 0, sizeof(pedido));

            if (!ler_todos(fd_fifo, &pedido, sizeof(pedido))) {
                continue;
            }
            terminar_strings_pedido(&pedido);

            printf("[Corretora] Pedido tipo %d recebido de '%s'.\n",
                   pedido.tipo, pedido.id_investidor);

            snprintf(mensagem_log, sizeof(mensagem_log),
                     "Pedido tipo %d recebido do investidor '%s'.",
                     pedido.tipo, pedido.id_investidor);
            gravar_log(log_corretora, mensagem_log);

            processar_pedido(
                &pedido,
                id_corretora,
                socket_bv,
                log_corretora,
                &bv_ativa
            );
        }
    }

    // 4. Encerramento limpo (agora realmente alcançável via SIGINT/SIGTERM ou fecho da BV)
    MensagemBolsaValores despedida;
    memset(&despedida, 0, sizeof(despedida));
    despedida.tipo = MSG_TERMINAR_SESSAO;
    copiar_texto(despedida.id_corretora, sizeof(despedida.id_corretora), id_corretora);
    if (bv_ativa) {
        escrever_todos(socket_bv, &despedida, sizeof(despedida));
    }

    close(fd_fifo);
    unlink(fifo_publico);
    close(socket_bv);
    gravar_log(log_corretora, "=== ENCERRAMENTO DA CORRETORA ===");
    pthread_mutex_destroy(&lista_carteiras.mutex);

    printf("Corretora '%s' encerrada de forma limpa.\n", id_corretora);
    return EXIT_SUCCESS;
}