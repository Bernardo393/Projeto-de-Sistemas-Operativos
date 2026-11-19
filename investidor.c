#include "bolsa.h"
#include "comunicacao.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char fifo_privado_global[MAX_PATH] = "";

static void tratar_sinal_saida(int sinal) {
    (void) sinal;

    if (fifo_privado_global[0] != '\0') {
        unlink(fifo_privado_global);
    }

    _exit(EXIT_FAILURE);
}

static void configurar_sinais(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_sinal_saida;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void limpar_buffer_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
    }
}

static void mostrar_menu(void) {
    printf("\n========================================\n");
    printf("        BOLSA DE VALORES - INVESTIDOR\n");
    printf("========================================\n");
    printf("1. Consultar cotações atuais\n");
    printf("2. Consultar carteira de ações\n");
    printf("3. Emitir ordem de compra\n");
    printf("4. Emitir ordem de venda\n");
    printf("5. Consultar ordens pendentes e transações\n");
    printf("6. Terminar sessão\n");
    printf("========================================\n");
    printf("Escolha uma opção: ");
}

static int inicializar_pedido_base(
    PedidoInvestidor *pedido,
    TipoMensagem tipo,
    const char *id_corretora,
    const char *id_investidor,
    const char *fifo_privado
) {
    memset(pedido, 0, sizeof(*pedido));
    pedido->tipo = tipo;

    return copiar_texto(pedido->id_corretora, sizeof(pedido->id_corretora), id_corretora) &&
           copiar_texto(pedido->id_investidor, sizeof(pedido->id_investidor), id_investidor) &&
           copiar_texto(pedido->fifo_resposta, sizeof(pedido->fifo_resposta), fifo_privado);
}

static int enviar_pedido(
    const char *fifo_publico,
    const PedidoInvestidor *pedido,
    RespostaCorretora *resposta
) {
    int fd_privado = -1;

    if (resposta != NULL) {
        /*
         * O_RDWR mantém uma extremidade de escrita aberta no próprio
         * investidor e evita EOF antes de a corretora responder.
         */
        fd_privado = open(pedido->fifo_resposta, O_RDWR);
        if (fd_privado < 0) {
            perror("Erro ao abrir o FIFO privado do investidor");
            return 0;
        }
    }

    // O_NONBLOCK evita congelar o investidor se o FIFO existir mas a corretora estiver morta
    int fd_publico = open(fifo_publico, O_WRONLY | O_NONBLOCK);

    if (fd_publico < 0) {
        if (errno == ENXIO || errno == ENOENT) {
            fprintf(stderr, "Erro: corretora offline ou indisponível.\n");
        } else {
            perror("Erro ao abrir o FIFO da corretora");
        }
        if (fd_privado >= 0) {
            close(fd_privado);
        }
        return 0;
    }

    // Repor modo bloqueante após confirmar que a corretora está ativa a escutar
    int flags = fcntl(fd_publico, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_publico, F_SETFL, flags & ~O_NONBLOCK);
    }

    int sucesso = escrever_todos(fd_publico, pedido, sizeof(*pedido));
    close(fd_publico);

    if (!sucesso) {
        perror("Erro ao enviar pedido para a corretora");
        if (fd_privado >= 0) {
            close(fd_privado);
        }
        return 0;
    }

    if (resposta == NULL) {
        return 1;
    }

    struct pollfd pfd = {
        .fd = fd_privado,
        .events = POLLIN
    };
    int pronto;

    do {
        pronto = poll(&pfd, 1, 5000);
    } while (pronto < 0 && errno == EINTR);

    if (pronto == 0) {
        fprintf(
            stderr,
            "Erro: tempo limite esgotado à espera da resposta da corretora.\n"
        );
        close(fd_privado);
        return 0;
    }

    if (pronto < 0) {
        perror("Erro ao aguardar resposta da corretora");
        close(fd_privado);
        return 0;
    }

    memset(resposta, 0, sizeof(*resposta));
    sucesso = ler_todos(fd_privado, resposta, sizeof(*resposta));
    close(fd_privado);

    if (!sucesso) {
        fprintf(stderr, "Erro: falha ao receber resposta completa da corretora.\n");
        return 0;
    }

    return 1;
}

static int ler_quantidade_positiva(const char *mensagem, int *valor) {
    printf("%s", mensagem);

    int lidos = scanf("%d", valor);
    if (lidos == EOF) {
        return -1;
    }

    limpar_buffer_stdin();

    if (lidos != 1 || *valor <= 0) {
        printf("Quantidade inválida. Introduza um número inteiro positivo.\n");
        return 0;
    }

    return 1;
}

static int ler_preco_positivo(const char *mensagem, float *valor) {
    printf("%s", mensagem);

    int lidos = scanf("%f", valor);
    if (lidos == EOF) {
        return -1;
    }

    limpar_buffer_stdin();

    // isfinite() bloqueia valores especiais IEEE-754 como "nan" ou "inf"
    if (lidos != 1 || !isfinite(*valor) || *valor <= 0.0f) {
        printf("Preço inválido. Introduza um valor numérico positivo.\n");
        return 0;
    }

    return 1;
}

static void mostrar_cotacoes(const RespostaCorretora *resposta) {
    if (resposta->num_elementos < 0 || resposta->num_elementos > MAX_EMPRESAS) {
        fprintf(stderr, "Resposta inválida: número de cotações fora dos limites.\n");
        return;
    }

    printf("\n--- TABELA DE COTAÇÕES ---\n");

    if (resposta->num_elementos == 0) {
        printf("Sem empresas cotadas disponíveis.\n");
        return;
    }

    for (int i = 0; i < resposta->num_elementos; i++) {
        printf(
            "Empresa: %-15s | Disponíveis: %-8d | Nominal: %-7.2f | Última cotação: %-7.2f\n",
            resposta->dados.cotacoes[i].nome,
            resposta->dados.cotacoes[i].acoes_disponiveis,
            resposta->dados.cotacoes[i].valor_nominal,
            resposta->dados.cotacoes[i].ultima_cotacao
        );
    }
}

static void mostrar_carteira(const RespostaCorretora *resposta) {
    const CarteiraInvestidor *carteira = &resposta->dados.carteira;

    if (carteira->num_ativos < 0 || carteira->num_ativos > MAX_EMPRESAS) {
        fprintf(stderr, "Resposta inválida: número de ativos na carteira fora dos limites.\n");
        return;
    }

    printf("\n--- CARTEIRA DE AÇÕES (%s) ---\n", carteira->id_investidor);

    if (carteira->num_ativos == 0) {
        printf("A carteira não possui ações de momento.\n");
        return;
    }

    for (int i = 0; i < carteira->num_ativos; i++) {
        printf(
            "Empresa: %-15s | Quantidade: %d\n",
            carteira->acoes[i].nome_empresa,
            carteira->acoes[i].quantidade
        );
    }
}

static void mostrar_ordens_e_transacoes(const RespostaCorretora *resposta) {
    const CarteiraInvestidor *carteira = &resposta->dados.carteira;

    if (carteira->num_ordens_pendentes < 0 ||
        carteira->num_ordens_pendentes > MAX_HISTORICO ||
        carteira->num_transacoes < 0 ||
        carteira->num_transacoes > MAX_HISTORICO) {
        fprintf(stderr, "Resposta inválida: limites de histórico inconsistentes.\n");
        return;
    }

    printf("\n--- ORDENS PENDENTES ---\n");
    if (carteira->num_ordens_pendentes == 0) {
        printf("Sem ordens pendentes.\n");
    } else {
        for (int i = 0; i < carteira->num_ordens_pendentes; i++) {
            const OrdemCV *ord = &carteira->ordens_pendentes[i];
            printf(
                "[%s] Empresa: %-15s | Qtd: %-6d | Preço pretendido: %.2f\n",
                (ord->tipo == 'C') ? "COMPRA" : "VENDA ",
                ord->nome_empresa,
                ord->quantidade,
                ord->preco
            );
        }
    }

    printf("\n--- TRANSAÇÕES REALIZADAS ---\n");
    if (carteira->num_transacoes == 0) {
        printf("Sem transações realizadas.\n");
    } else {
        for (int i = 0; i < carteira->num_transacoes; i++) {
            const NotificacaoOrdemCV *tr = &carteira->transacoes_realizadas[i];
            printf(
                "Empresa: %-15s | Qtd: %-6d | Preço definitivo: %.2f\n",
                tr->nome_empresa,
                tr->quantidade,
                tr->preco_definitivo
            );
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso correto: %s <id_corretora> <id_investidor>\n", argv[0]);
        return EXIT_FAILURE;
    }

    configurar_sinais();

    char id_corretora[MAX_NOME];
    char id_investidor[MAX_NOME];

    if (!copiar_texto(id_corretora, sizeof(id_corretora), argv[1]) ||
        !copiar_texto(id_investidor, sizeof(id_investidor), argv[2])) {
        fprintf(stderr, "Erro: identificador demasiado comprido.\n");
        return EXIT_FAILURE;
    }

    char fifo_publico[MAX_PATH];
    char fifo_privado[MAX_PATH];

    int tamanho = snprintf(
        fifo_publico, sizeof(fifo_publico),
        "%s%s", FIFO_CORRETORA_PREFIX, id_corretora
    );
    if (tamanho < 0 || (size_t) tamanho >= sizeof(fifo_publico)) {
        fprintf(stderr, "Erro: caminho do FIFO da corretora demasiado longo.\n");
        return EXIT_FAILURE;
    }

    tamanho = snprintf(
        fifo_privado, sizeof(fifo_privado),
        "%s%s_%ld", FIFO_INVESTIDOR_PREFIX, id_corretora, (long) getpid()
    );
    if (tamanho < 0 || (size_t) tamanho >= sizeof(fifo_privado)) {
        fprintf(stderr, "Erro: caminho do FIFO privado demasiado longo.\n");
        return EXIT_FAILURE;
    }

    unlink(fifo_privado);
    if (mkfifo(fifo_privado, 0600) < 0) {
        perror("Erro ao criar o FIFO privado do investidor");
        return EXIT_FAILURE;
    }

    if (!copiar_texto(fifo_privado_global, sizeof(fifo_privado_global), fifo_privado)) {
        fprintf(stderr, "Erro ao guardar o caminho do FIFO privado.\n");
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    // 1. Login com confirmação obrigatória da corretora
    PedidoInvestidor pedido;
    RespostaCorretora resposta;

    if (!inicializar_pedido_base(
            &pedido, MSG_LOGIN_INVESTIDOR, id_corretora, id_investidor, fifo_privado
        )) {
        fprintf(stderr, "Erro ao preparar pedido de login.\n");
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    if (!enviar_pedido(fifo_publico, &pedido, &resposta)) {
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    if (resposta.tipo != MSG_RESPOSTA_OK) {
        fprintf(stderr, "Login rejeitado pela corretora: %s\n", resposta.mensagem);
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    printf("\nInvestidor '%s' ligado à corretora '%s': %s\n",
           id_investidor, id_corretora, resposta.mensagem);

    // 2. Ciclo principal de interação
    int opcao = 0;

    do {
        mostrar_menu();

        int lidos = scanf("%d", &opcao);
        if (lidos == EOF) {
            opcao = 6; // Trata Ctrl+D como saída ordeira
        } else if (lidos != 1) {
            printf("Opção inválida.\n");
            limpar_buffer_stdin();
            continue;
        } else {
            limpar_buffer_stdin();
        }

        if (!inicializar_pedido_base(
                &pedido, MSG_INVALIDA, id_corretora, id_investidor, fifo_privado
            )) {
            fprintf(stderr, "Erro interno ao preparar o pedido.\n");
            break;
        }

        switch (opcao) {
            case 1:
                pedido.tipo = MSG_LISTAR_COTACOES;
                break;

            case 2:
                pedido.tipo = MSG_CONSULTAR_CARTEIRA;
                break;

            case 3:
            case 4: {
                pedido.tipo = MSG_EMITIR_ORDEM;
                pedido.ordem.tipo = (opcao == 3) ? 'C' : 'V';

                if (!copiar_texto(pedido.ordem.corretora, sizeof(pedido.ordem.corretora), id_corretora) ||
                    !copiar_texto(pedido.ordem.investidor, sizeof(pedido.ordem.investidor), id_investidor)) {
                    fprintf(stderr, "Erro ao preparar os identificadores da ordem.\n");
                    continue;
                }

                printf("Nome da empresa: ");
                int res_nome = scanf("%29s", pedido.ordem.nome_empresa);
                if (res_nome == EOF) {
                    opcao = 6;
                    pedido.tipo = MSG_TERMINAR_SESSAO;
                    break;
                }
                limpar_buffer_stdin();
                if (res_nome != 1) {
                    printf("Nome da empresa inválido.\n");
                    continue;
                }

                int res_qtd = ler_quantidade_positiva("Quantidade de ações: ", &pedido.ordem.quantidade);
                if (res_qtd < 0) {
                    opcao = 6;
                    pedido.tipo = MSG_TERMINAR_SESSAO;
                    break;
                }
                if (res_qtd == 0) {
                    continue;
                }

                int res_preco = ler_preco_positivo("Preço unitário pretendido: ", &pedido.ordem.preco);
                if (res_preco < 0) {
                    opcao = 6;
                    pedido.tipo = MSG_TERMINAR_SESSAO;
                    break;
                }
                if (res_preco == 0) {
                    continue;
                }
                break;
            }

            case 5:
                pedido.tipo = MSG_CONSULTAR_ORDENS;
                break;

            case 6:
                pedido.tipo = MSG_TERMINAR_SESSAO;
                break;

            default:
                printf("Opção desconhecida.\n");
                continue;
        }

        if (opcao == 6) {
            if (!enviar_pedido(fifo_publico, &pedido, &resposta)) {
                fprintf(stderr, "Aviso: não foi possível confirmar o fecho junto da corretora.\n");
            } else {
                printf("\n[RESPOSTA DA CORRETORA] %s\n", resposta.mensagem);
            }
            break;
        }

        if (!enviar_pedido(fifo_publico, &pedido, &resposta)) {
            continue;
        }

        printf("\n[RESPOSTA DA CORRETORA] %s\n", resposta.mensagem);

        if (resposta.tipo != MSG_RESPOSTA_OK) {
            continue;
        }

        if (opcao == 1) {
            mostrar_cotacoes(&resposta);
        } else if (opcao == 2) {
            mostrar_carteira(&resposta);
        } else if (opcao == 5) {
            mostrar_ordens_e_transacoes(&resposta);
        }

    } while (opcao != 6);

    unlink(fifo_privado);
    fifo_privado_global[0] = '\0';
    printf("Sessão terminada.\n");
    return EXIT_SUCCESS;
}