#include "bolsa.h"
#include "comunicacao.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static TabelaCotacoes mercado;
static BufferOrdensPendentes buffer_ordens;
static OrdensNaoResolvidasEmpresa ordens_empresa[MAX_EMPRESAS];
static DescritorCorretora corretoras[MAX_CORRETORAS];

static pthread_mutex_t mutex_corretoras = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_ordens_empresa = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t em_execucao = 1;
static int socket_servidor = -1;

static void terminar_strings_mensagem(MensagemBolsaValores *mensagem) {
    mensagem->id_corretora[MAX_NOME - 1] = '\0';

    if (mensagem->tipo == MSG_EMITIR_ORDEM) {
        mensagem->dados.ordem.corretora[MAX_NOME - 1] = '\0';
        mensagem->dados.ordem.investidor[MAX_NOME - 1] = '\0';
        mensagem->dados.ordem.nome_empresa[MAX_NOME - 1] = '\0';
    }
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
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static int enviar_mensagem(int fd, const MensagemBolsaValores *mensagem) {
    return escrever_todos(fd, mensagem, sizeof(*mensagem));
}

static void preparar_resposta(
    const MensagemBolsaValores *pedido,
    MensagemBolsaValores *resposta
) {
    memset(resposta, 0, sizeof(*resposta));
    resposta->tipo = MSG_RESPOSTA_OK;
    copiar_texto(
        resposta->id_corretora,
        sizeof(resposta->id_corretora),
        pedido->id_corretora
    );
}

static int inicializar_estado(
    const char *ficheiro_empresas,
    const char *log_servidor
) {
    memset(&mercado, 0, sizeof(mercado));
    memset(&buffer_ordens, 0, sizeof(buffer_ordens));
    memset(ordens_empresa, 0, sizeof(ordens_empresa));
    memset(corretoras, 0, sizeof(corretoras));

    for (int i = 0; i < MAX_CORRETORAS; i++) {
        corretoras[i].fd_socket = -1;
        corretoras[i].fd_pipe[0] = -1;
        corretoras[i].fd_pipe[1] = -1;
    }

    if (pthread_rwlock_init(&mercado.lock, NULL) != 0 ||
        pthread_mutex_init(&buffer_ordens.mutex, NULL) != 0 ||
        pthread_cond_init(&buffer_ordens.cond_nao_cheio, NULL) != 0 ||
        pthread_cond_init(&buffer_ordens.cond_nao_vazio, NULL) != 0) {
        gravar_log(log_servidor, "Erro crítico: falha ao inicializar estruturas de sincronização.");
        return 0;
    }

    if (!carregar_empresas(
            ficheiro_empresas,
            mercado.empresas,
            &mercado.num_empresas,
            log_servidor
        )) {
        return 0;
    }

    for (int i = 0; i < mercado.num_empresas; i++) {
        copiar_texto(
            ordens_empresa[i].nome_empresa,
            sizeof(ordens_empresa[i].nome_empresa),
            mercado.empresas[i].nome
        );
    }

    return 1;
}

static int criar_socket_servidor(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Servidor: erro ao criar socket");
        return -1;
    }

    int reutilizar = 1;
    if (setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reutilizar,
            sizeof(reutilizar)
        ) < 0) {
        perror("Servidor: erro em setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco.sin_port = htons(PORTA_BV);

    if (bind(fd, (struct sockaddr *)&endereco, sizeof(endereco)) < 0) {
        perror("Servidor: erro em bind");
        close(fd);
        return -1;
    }

    if (listen(fd, MAX_CORRETORAS) < 0) {
        perror("Servidor: erro em listen");
        close(fd);
        return -1;
    }

    return fd;
}

static int encontrar_corretora_por_id(const char *id_corretora) {
    for (int i = 0; i < MAX_CORRETORAS; i++) {
        if (corretoras[i].ativa &&
            strcmp(corretoras[i].id_corretora, id_corretora) == 0) {
            return i;
        }
    }

    return -1;
}

static void recolher_corretoras_terminadas(void) {
    for (int i = 0; i < MAX_CORRETORAS; i++) {
        pthread_t thread;
        int precisa_join = 0;

        pthread_mutex_lock(&mutex_corretoras);
        if (!corretoras[i].ativa && corretoras[i].thread_criada) {
            thread = corretoras[i].thread;
            corretoras[i].thread_criada = 0;
            precisa_join = 1;
        }
        pthread_mutex_unlock(&mutex_corretoras);

        if (precisa_join) {
            pthread_join(thread, NULL);
        }
    }
}

static int reservar_corretora(
    int fd_socket,
    const char *id_corretora
) {
    recolher_corretoras_terminadas();
    pthread_mutex_lock(&mutex_corretoras);

    if (encontrar_corretora_por_id(id_corretora) >= 0) {
        pthread_mutex_unlock(&mutex_corretoras);
        return -2;
    }

    for (int i = 0; i < MAX_CORRETORAS; i++) {
        if (!corretoras[i].ativa && !corretoras[i].thread_criada) {
            memset(&corretoras[i], 0, sizeof(corretoras[i]));
            corretoras[i].fd_socket = -1;
            corretoras[i].fd_pipe[0] = -1;
            corretoras[i].fd_pipe[1] = -1;
            copiar_texto(
                corretoras[i].id_corretora,
                sizeof(corretoras[i].id_corretora),
                id_corretora
            );
            corretoras[i].fd_socket = fd_socket;
            corretoras[i].ativa = 1;
            pthread_mutex_unlock(&mutex_corretoras);
            return i;
        }
    }

    pthread_mutex_unlock(&mutex_corretoras);
    return -1;
}

static void libertar_corretora(DescritorCorretora *corretora) {
    pthread_mutex_lock(&mutex_corretoras);
    corretora->ativa = 0;
    corretora->fd_socket = -1;
    pthread_mutex_unlock(&mutex_corretoras);
}

static int encontrar_empresa(const char *nome_empresa) {
    for (int i = 0; i < mercado.num_empresas; i++) {
        if (strcmp(mercado.empresas[i].nome, nome_empresa) == 0) {
            return i;
        }
    }

    return -1;
}

static int inserir_ordem_buffer(const OrdemCV *ordem) {
    pthread_mutex_lock(&buffer_ordens.mutex);

    while (buffer_ordens.count == MAX_ORDENS_BUFFER && em_execucao) {
        pthread_cond_wait(&buffer_ordens.cond_nao_cheio, &buffer_ordens.mutex);
    }

    if (!em_execucao) {
        pthread_mutex_unlock(&buffer_ordens.mutex);
        return 0;
    }

    buffer_ordens.buffer[buffer_ordens.in] = *ordem;
    buffer_ordens.in = (buffer_ordens.in + 1) % MAX_ORDENS_BUFFER;
    buffer_ordens.count++;
    pthread_cond_signal(&buffer_ordens.cond_nao_vazio);
    pthread_mutex_unlock(&buffer_ordens.mutex);
    return 1;
}

static void guardar_ordem_pendente(const OrdemCV *ordem) {
    pthread_rwlock_rdlock(&mercado.lock);
    int indice = encontrar_empresa(ordem->nome_empresa);
    pthread_rwlock_unlock(&mercado.lock);

    if (indice < 0) {
        return;
    }

    pthread_mutex_lock(&mutex_ordens_empresa);

    OrdensNaoResolvidasEmpresa *pendentes = &ordens_empresa[indice];
    OrdemCV *destino;
    int *quantidade;

    if (ordem->tipo == 'C') {
        destino = pendentes->compras;
        quantidade = &pendentes->num_compras;
    } else {
        destino = pendentes->vendas;
        quantidade = &pendentes->num_vendas;
    }

    if (*quantidade < MAX_ORDENS_NR) {
        destino[*quantidade] = *ordem;
        (*quantidade)++;
    } else {
        gravar_log(
            "eventos_servidor.txt",
            "Aviso: limite de ordens pendentes atingido para uma empresa."
        );
    }

    pthread_mutex_unlock(&mutex_ordens_empresa);
}

static void *tarefa_ordens_pendentes(void *argumento) {
    (void) argumento;

    while (em_execucao) {
        OrdemCV ordem;

        pthread_mutex_lock(&buffer_ordens.mutex);
        while (buffer_ordens.count == 0 && em_execucao) {
            pthread_cond_wait(&buffer_ordens.cond_nao_vazio, &buffer_ordens.mutex);
        }

        if (!em_execucao) {
            pthread_mutex_unlock(&buffer_ordens.mutex);
            break;
        }

        ordem = buffer_ordens.buffer[buffer_ordens.out];
        buffer_ordens.out = (buffer_ordens.out + 1) % MAX_ORDENS_BUFFER;
        buffer_ordens.count--;
        pthread_cond_signal(&buffer_ordens.cond_nao_cheio);
        pthread_mutex_unlock(&buffer_ordens.mutex);

        guardar_ordem_pendente(&ordem);
    }

    return NULL;
}

static void responder_cotacoes(
    const MensagemBolsaValores *pedido,
    MensagemBolsaValores *resposta
) {
    pthread_rwlock_rdlock(&mercado.lock);
    resposta->num_elementos = mercado.num_empresas;
    memcpy(
        resposta->dados.cotacoes,
        mercado.empresas,
        (size_t) mercado.num_empresas * sizeof(Empresa)
    );
    pthread_rwlock_unlock(&mercado.lock);

    copiar_texto(
        resposta->mensagem,
        sizeof(resposta->mensagem),
        "Cotações consultadas com sucesso."
    );
    copiar_texto(
        resposta->id_corretora,
        sizeof(resposta->id_corretora),
        pedido->id_corretora
    );
}

static void processar_pedido(
    const MensagemBolsaValores *pedido,
    MensagemBolsaValores *resposta,
    const char *log_servidor
) {
    preparar_resposta(pedido, resposta);

    switch (pedido->tipo) {
        case MSG_LISTAR_COTACOES:
            responder_cotacoes(pedido, resposta);
            break;

        case MSG_EMITIR_ORDEM: {
            pthread_rwlock_rdlock(&mercado.lock);
            int ordem_valida = validar_ordem(
                pedido->dados.ordem,
                mercado.empresas,
                mercado.num_empresas,
                log_servidor
            );
            pthread_rwlock_unlock(&mercado.lock);

            if (!ordem_valida) {
                resposta->tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(
                    resposta->mensagem,
                    sizeof(resposta->mensagem),
                    "Ordem inválida."
                );
            } else if (!inserir_ordem_buffer(&pedido->dados.ordem)) {
                resposta->tipo = MSG_RESPOSTA_ERRO;
                copiar_texto(
                    resposta->mensagem,
                    sizeof(resposta->mensagem),
                    "Servidor em processo de encerramento."
                );
            } else {
                copiar_texto(
                    resposta->mensagem,
                    sizeof(resposta->mensagem),
                    "Ordem recebida e colocada em espera."
                );
            }
            break;
        }

        case MSG_TERMINAR_SESSAO:
            copiar_texto(
                resposta->mensagem,
                sizeof(resposta->mensagem),
                "Ligação da corretora encerrada."
            );
            break;

        default:
            resposta->tipo = MSG_RESPOSTA_ERRO;
            copiar_texto(
                resposta->mensagem,
                sizeof(resposta->mensagem),
                "Operação desconhecida pelo servidor."
            );
            break;
    }
}

static void *atender_corretora(void *argumento) {
    DescritorCorretora *corretora = argumento;
    MensagemBolsaValores pedido;
    char id_corretora[MAX_NOME];

    copiar_texto(
        id_corretora,
        sizeof(id_corretora),
        corretora->id_corretora
    );

    while (em_execucao && corretora->ativa) {
        if (!ler_todos(corretora->fd_socket, &pedido, sizeof(pedido))) {
            break;
        }
        terminar_strings_mensagem(&pedido);
        copiar_texto(
            pedido.id_corretora,
            sizeof(pedido.id_corretora),
            id_corretora
        );
        if (pedido.tipo == MSG_EMITIR_ORDEM) {
            copiar_texto(
                pedido.dados.ordem.corretora,
                sizeof(pedido.dados.ordem.corretora),
                id_corretora
            );
        }

        MensagemBolsaValores resposta;
        processar_pedido(&pedido, &resposta, "eventos_servidor.txt");

        if (!enviar_mensagem(corretora->fd_socket, &resposta)) {
            break;
        }

        if (pedido.tipo == MSG_TERMINAR_SESSAO) {
            break;
        }
    }

    int fd_socket = corretora->fd_socket;
    libertar_corretora(corretora);

    if (fd_socket >= 0) {
        shutdown(fd_socket, SHUT_RDWR);
        close(fd_socket);
    }

    char mensagem_log[160];
    snprintf(
        mensagem_log,
        sizeof(mensagem_log),
        "Corretora '%s' desligada.",
        id_corretora
    );
    gravar_log("eventos_servidor.txt", mensagem_log);
    return NULL;
}

static int registar_corretora(
    int fd_socket,
    const MensagemBolsaValores *registo,
    MensagemBolsaValores *resposta
) {
    if (registo->tipo != MSG_REGISTAR_CORRETORA ||
        registo->id_corretora[0] == '\0') {
        printf("Registo de corretora rejeitado: pedido inválido.\n");
        gravar_log("eventos_servidor.txt", "Registo de corretora rejeitado: pedido inválido.");
        resposta->tipo = MSG_RESPOSTA_ERRO;
        copiar_texto(
            resposta->mensagem,
            sizeof(resposta->mensagem),
            "Pedido de registo inválido."
        );
        return 0;
    }

    int indice = reservar_corretora(fd_socket, registo->id_corretora);
    if (indice == -2) {
        char mensagem_log[160];
        snprintf(
            mensagem_log,
            sizeof(mensagem_log),
            "Registo de corretora '%s' rejeitado: identificador duplicado.",
            registo->id_corretora
        );
        printf("Registo de corretora '%s' rejeitado: identificador duplicado.\n", registo->id_corretora);
        gravar_log("eventos_servidor.txt", mensagem_log);
        resposta->tipo = MSG_RESPOSTA_ERRO;
        copiar_texto(
            resposta->mensagem,
            sizeof(resposta->mensagem),
            "Já existe uma corretora com esse identificador."
        );
        return 0;
    }

    if (indice < 0) {
        char mensagem_log[160];
        snprintf(
            mensagem_log,
            sizeof(mensagem_log),
            "Registo de corretora '%s' rejeitado: limite máximo atingido.",
            registo->id_corretora
        );
        printf("Registo de corretora '%s' rejeitado: limite atingido.\n", registo->id_corretora);
        gravar_log("eventos_servidor.txt", mensagem_log);
        resposta->tipo = MSG_RESPOSTA_ERRO;
        copiar_texto(
            resposta->mensagem,
            sizeof(resposta->mensagem),
            "Limite máximo de corretoras atingido."
        );
        return 0;
    }

    resposta->tipo = MSG_RESPOSTA_OK;
    copiar_texto(
        resposta->id_corretora,
        sizeof(resposta->id_corretora),
        registo->id_corretora
    );
    copiar_texto(
        resposta->mensagem,
        sizeof(resposta->mensagem),
        "Corretora registada com sucesso."
    );

    if (!enviar_mensagem(fd_socket, resposta)) {
        libertar_corretora(&corretoras[indice]);
        return -1;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, atender_corretora, &corretoras[indice]) != 0) {
        libertar_corretora(&corretoras[indice]);
        return -1;
    }

    pthread_mutex_lock(&mutex_corretoras);
    corretoras[indice].thread = thread;
    corretoras[indice].thread_criada = 1;
    pthread_mutex_unlock(&mutex_corretoras);

    printf("Corretora '%s' registada.\n", registo->id_corretora);
    char mensagem_log[160];
    snprintf(
        mensagem_log,
        sizeof(mensagem_log),
        "Corretora '%s' registada com sucesso.",
        registo->id_corretora
    );
    gravar_log("eventos_servidor.txt", mensagem_log);
    return 1;
}

static void encerrar_corretoras(void) {
    pthread_mutex_lock(&mutex_corretoras);

    for (int i = 0; i < MAX_CORRETORAS; i++) {
        if (corretoras[i].ativa) {
            shutdown(corretoras[i].fd_socket, SHUT_RDWR);
        }
    }

    pthread_mutex_unlock(&mutex_corretoras);

    for (int i = 0; i < MAX_CORRETORAS; i++) {
        if (corretoras[i].thread_criada) {
            pthread_join(corretoras[i].thread, NULL);
            corretoras[i].thread_criada = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    const char *log_servidor = "eventos_servidor.txt";
    const char *ficheiro_empresas = "bvdata.txt";
    pthread_t thread_ordens;

    if (argc > 2) {
        fprintf(stderr, "Uso correto: %s [ficheiro_bvdata]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        ficheiro_empresas = argv[1];
    }

    configurar_sinais();
    gravar_log(log_servidor, "=== ARRANQUE DO SERVIDOR DA BOLSA ===");

    if (!inicializar_estado(ficheiro_empresas, log_servidor)) {
        fprintf(stderr, "Falha ao inicializar o servidor.\n");
        gravar_log(log_servidor, "=== ENCERRAMENTO POR FALHA CRÍTICA ===");
        return EXIT_FAILURE;
    }

    socket_servidor = criar_socket_servidor();
    if (socket_servidor < 0) {
        gravar_log(log_servidor, "Erro crítico: não foi possível criar o socket da Bolsa.");
        return EXIT_FAILURE;
    }

    if (pthread_create(&thread_ordens, NULL, tarefa_ordens_pendentes, NULL) != 0) {
        fprintf(stderr, "Erro: não foi possível criar a tarefa de ordens.\n");
        gravar_log(log_servidor, "Erro crítico: não foi possível criar a tarefa de ordens.");
        close(socket_servidor);
        return EXIT_FAILURE;
    }

    printf(
        "Servidor iniciado na porta %d com %d empresas cotadas.\n",
        PORTA_BV,
        mercado.num_empresas
    );

    while (em_execucao) {
        recolher_corretoras_terminadas();

        struct pollfd descritor_poll = {
            .fd = socket_servidor,
            .events = POLLIN,
            .revents = 0
        };
        int resultado_poll = poll(&descritor_poll, 1, 500);
        if (resultado_poll == 0) {
            continue;
        }
        if (resultado_poll < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Servidor: erro em poll");
            break;
        }

        struct sockaddr_in endereco_cliente;
        socklen_t tamanho_endereco = sizeof(endereco_cliente);
        int fd_corretora = accept(
            socket_servidor,
            (struct sockaddr *)&endereco_cliente,
            &tamanho_endereco
        );

        if (fd_corretora < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (em_execucao) {
                perror("Servidor: erro em accept");
            }
            break;
        }

        MensagemBolsaValores registo;
        MensagemBolsaValores resposta;
        memset(&registo, 0, sizeof(registo));
        memset(&resposta, 0, sizeof(resposta));

        struct timeval tempo_limite = {5, 0};
        setsockopt(
            fd_corretora,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tempo_limite,
            sizeof(tempo_limite)
        );

        if (!ler_todos(fd_corretora, &registo, sizeof(registo))) {
            close(fd_corretora);
            continue;
        }
        terminar_strings_mensagem(&registo);

        struct timeval sem_limite = {0, 0};
        setsockopt(
            fd_corretora,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &sem_limite,
            sizeof(sem_limite)
        );

        preparar_resposta(&registo, &resposta);
        int resultado_registo = registar_corretora(
            fd_corretora,
            &registo,
            &resposta
        );

        if (resultado_registo == 0) {
            enviar_mensagem(fd_corretora, &resposta);
            close(fd_corretora);
        } else if (resultado_registo < 0) {
            close(fd_corretora);
        }
    }

    em_execucao = 0;
    pthread_mutex_lock(&buffer_ordens.mutex);
    pthread_cond_broadcast(&buffer_ordens.cond_nao_vazio);
    pthread_cond_broadcast(&buffer_ordens.cond_nao_cheio);
    pthread_mutex_unlock(&buffer_ordens.mutex);

    if (socket_servidor >= 0) {
        close(socket_servidor);
    }

    encerrar_corretoras();
    pthread_join(thread_ordens, NULL);

    if (!guardar_empresas(
            ficheiro_empresas,
            mercado.empresas,
            mercado.num_empresas,
            log_servidor
        )) {
        fprintf(stderr, "Aviso: não foi possível guardar o estado do mercado.\n");
    }

    pthread_cond_destroy(&buffer_ordens.cond_nao_cheio);
    pthread_cond_destroy(&buffer_ordens.cond_nao_vazio);
    pthread_mutex_destroy(&buffer_ordens.mutex);
    pthread_mutex_destroy(&mutex_ordens_empresa);
    pthread_mutex_destroy(&mutex_corretoras);
    pthread_rwlock_destroy(&mercado.lock);

    gravar_log(log_servidor, "=== ENCERRAMENTO DO SERVIDOR ===");
    return EXIT_SUCCESS;
}
