#include "bolsa.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void limpar_buffer_stdin(void) {
    int c;

    while ((c = getchar()) != '\n' && c != EOF) {
    }
}

static int escrever_todos(int fd, const void *dados, size_t tamanho) {
    const char *ponteiro = dados;
    size_t escritos = 0;

    while (escritos < tamanho) {
        ssize_t resultado = write(fd, ponteiro + escritos, tamanho - escritos);

        if (resultado < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }

        if (resultado == 0) {
            return 0;
        }

        escritos += (size_t) resultado;
    }

    return 1;
}

static int ler_todos(int fd, void *dados, size_t tamanho) {
    char *ponteiro = dados;
    size_t lidos = 0;

    while (lidos < tamanho) {
        ssize_t resultado = read(fd, ponteiro + lidos, tamanho - lidos);

        if (resultado < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }

        if (resultado == 0) {
            return 0;
        }

        lidos += (size_t) resultado;
    }

    return 1;
}

static int copiar_texto(char destino[], size_t tamanho, const char *origem) {
    int escritos = snprintf(destino, tamanho, "%s", origem);
    return escritos >= 0 && (size_t) escritos < tamanho;
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

static int enviar_pedido(const char *fifo_publico, const MensagemProtocolo *pedido, MensagemProtocolo *resposta) {
    int fd_publico = open(fifo_publico, O_WRONLY);

    if (fd_publico < 0) {
        perror("Erro ao abrir o FIFO da corretora");
        return 0;
    }

    int sucesso = escrever_todos(fd_publico, pedido, sizeof(*pedido));
    close(fd_publico);

    if (!sucesso) {
        perror("Erro ao enviar pedido para a corretora");
        return 0;
    }

    if (resposta == NULL) {
        return 1;
    }

    int fd_privado = open(pedido->fifo_resposta, O_RDONLY);

    if (fd_privado < 0) {
        perror("Erro ao abrir o FIFO privado do investidor");
        return 0;
    }

    sucesso = ler_todos(fd_privado, resposta, sizeof(*resposta));
    close(fd_privado);

    if (!sucesso) {
        perror("Erro ao receber resposta da corretora");
        return 0;
    }

    return 1;
}

static int ler_inteiro(const char *mensagem, int *valor) {
    printf("%s", mensagem);

    if (scanf("%d", valor) != 1) {
        printf("Valor inválido.\n");
        limpar_buffer_stdin();
        return 0;
    }

    limpar_buffer_stdin();
    return 1;
}

static int ler_preco(const char *mensagem, float *valor) {
    printf("%s", mensagem);

    if (scanf("%f", valor) != 1) {
        printf("Preço inválido.\n");
        limpar_buffer_stdin();
        return 0;
    }

    limpar_buffer_stdin();
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(
            stderr,
            "Uso correto: %s <id_corretora> <id_investidor>\n",
            argv[0]
        );
        return EXIT_FAILURE;
    }

    char id_corretora[MAX_NOME];
    char id_investidor[MAX_NOME];

    if (!copiar_texto(id_corretora, sizeof(id_corretora), argv[1]) ||
        !copiar_texto(id_investidor, sizeof(id_investidor), argv[2])) {
        fprintf(stderr, "Erro: identificador demasiado comprido.\n");
        return EXIT_FAILURE;
    }

    char fifo_publico[128];
    char fifo_privado[128];

    int tamanho = snprintf(
        fifo_publico,
        sizeof(fifo_publico),
        "%s%s",
        FIFO_CORRETORA_PREFIX,
        id_corretora
    );

    if (tamanho < 0 || (size_t) tamanho >= sizeof(fifo_publico)) {
        fprintf(stderr, "Erro: caminho do FIFO da corretora demasiado longo.\n");
        return EXIT_FAILURE;
    }

    tamanho = snprintf(
        fifo_privado,
        sizeof(fifo_privado),
        "%s%s_%ld",
        FIFO_INVESTIDOR_PREFIX,
        id_corretora,
        (long) getpid()
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

    MensagemProtocolo mensagem;
    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo = MSG_LOGIN_INVESTIDOR;

    if (!copiar_texto(
            mensagem.id_corretora,
            sizeof(mensagem.id_corretora),
            id_corretora
        ) ||
        !copiar_texto(
            mensagem.id_investidor,
            sizeof(mensagem.id_investidor),
            id_investidor
        ) ||
        !copiar_texto(
            mensagem.fifo_resposta,
            sizeof(mensagem.fifo_resposta),
            fifo_privado
        )) {
        fprintf(stderr, "Erro: dados da sessão demasiado longos.\n");
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    if (!enviar_pedido(fifo_publico, &mensagem, NULL)) {
        unlink(fifo_privado);
        return EXIT_FAILURE;
    }

    printf(
        "\nInvestidor '%s' ligado à corretora '%s'.\n",
        id_investidor,
        id_corretora
    );

    int opcao = 0;

    do {
        mostrar_menu();

        if (scanf("%d", &opcao) != 1) {
            printf("Opção inválida.\n");
            limpar_buffer_stdin();
            continue;
        }

        limpar_buffer_stdin();

        memset(&mensagem, 0, sizeof(mensagem));
        //alterar depois
        mensagem.tipo = MSG_RESPOSTA_ERRO;

        if (!copiar_texto(
                mensagem.id_corretora,
                sizeof(mensagem.id_corretora),
                id_corretora
            ) ||
            !copiar_texto(
                mensagem.id_investidor,
                sizeof(mensagem.id_investidor),
                id_investidor
            ) ||
            !copiar_texto(
                mensagem.fifo_resposta,
                sizeof(mensagem.fifo_resposta),
                fifo_privado
            )) {
            fprintf(stderr, "Erro ao preparar a mensagem.\n");
            break;
        }

        switch (opcao) {
            case 1:
                mensagem.tipo = MSG_LISTAR_COTACOES;
                break;

            case 2:
                mensagem.tipo = MSG_CONSULTAR_CARTEIRA;
                break;

            case 3:
            case 4:
                mensagem.tipo = MSG_EMITIR_ORDEM;
                mensagem.dados.ordem.tipo = opcao == 3 ? 'C' : 'V';

                if (!copiar_texto(
                        mensagem.dados.ordem.corretora,
                        sizeof(mensagem.dados.ordem.corretora),
                        id_corretora
                    ) ||
                    !copiar_texto(
                        mensagem.dados.ordem.investidor,
                        sizeof(mensagem.dados.ordem.investidor),
                        id_investidor
                    )) {
                    fprintf(stderr, "Erro ao preparar a ordem.\n");
                    continue;
                }

                printf("Nome da empresa: ");
                if (scanf("%29s", mensagem.dados.ordem.nome_empresa) != 1) {
                    printf("Nome da empresa inválido.\n");
                    limpar_buffer_stdin();
                    continue;
                }
                limpar_buffer_stdin();

                if (!ler_inteiro(
                        "Quantidade de ações: ",
                        &mensagem.dados.ordem.quantidade
                    ) ||
                    !ler_preco(
                        "Preço unitário pretendido: ",
                        &mensagem.dados.ordem.preco
                    )) {
                    continue;
                }
                break;

            case 5:
                mensagem.tipo = MSG_CONSULTAR_ORDENS;
                break;

            case 6:
                mensagem.tipo = MSG_TERMINAR_SESSAO;
                break;

            default:
                printf("Opção desconhecida.\n");
                continue;
        }

        if (opcao == 6) {
            enviar_pedido(fifo_publico, &mensagem, NULL);
            break;
        }

        MensagemProtocolo resposta;
        memset(&resposta, 0, sizeof(resposta));

        if (!enviar_pedido(fifo_publico, &mensagem, &resposta)) {
            continue;
        }

        printf("\n[RESPOSTA DA CORRETORA] %s\n", resposta.mensagem);

        if (opcao == 1 && resposta.tipo == MSG_RESPOSTA_OK) {
            if (resposta.num_elementos < 0 ||
                resposta.num_elementos > MAX_EMPRESAS) {
                fprintf(stderr, "Resposta inválida: número de cotações inválido.\n");
                continue;
            }

            printf("\n--- TABELA DE COTAÇÕES ---\n");

            for (int i = 0; i < resposta.num_elementos; i++) {
                printf(
                    "Empresa: %-10s | Disponíveis: %-6d | "
                    "Nominal: %-6.2f | Última cotação: %-6.2f\n",
                    resposta.dados.cotacoes[i].nome,
                    resposta.dados.cotacoes[i].acoes_disponiveis,
                    resposta.dados.cotacoes[i].valor_nominal,
                    resposta.dados.cotacoes[i].ultima_cotacao
                );
            }
        }
    } while (opcao != 6);

    unlink(fifo_privado);
    printf("Sessão terminada. Adeus!\n");
    return EXIT_SUCCESS;
}
