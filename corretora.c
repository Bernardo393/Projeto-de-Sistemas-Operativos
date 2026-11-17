#include "bolsa.h"
#include "comunicacao.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static ListaCarteiras lista_carteiras;

static void inicializar_corretora(void) {
    memset(&lista_carteiras, 0, sizeof(lista_carteiras));

    if (pthread_mutex_init(&lista_carteiras.mutex, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar o mutex das carteiras.\n");
        exit(EXIT_FAILURE);
    }
}

static CarteiraInvestidor *obter_ou_criar_carteira(const char *id_investidor) {
    pthread_mutex_lock(&lista_carteiras.mutex);

    for (int i = 0; i < lista_carteiras.num_carteiras; i++) {
        if (strcmp(
                lista_carteiras.carteiras[i].id_investidor,
                id_investidor
            ) == 0) {
            CarteiraInvestidor *carteira = &lista_carteiras.carteiras[i];

            pthread_mutex_unlock(&lista_carteiras.mutex);
            return carteira;
        }
    }

    if (lista_carteiras.num_carteiras >= MAX_INVESTIDORES) {
        pthread_mutex_unlock(&lista_carteiras.mutex);
        return NULL;
    }

    CarteiraInvestidor *nova_carteira =
        &lista_carteiras.carteiras[lista_carteiras.num_carteiras];

    memset(nova_carteira, 0, sizeof(*nova_carteira));

    if (!copiar_texto(
            nova_carteira->id_investidor,
            sizeof(nova_carteira->id_investidor),
            id_investidor
        )) {
        pthread_mutex_unlock(&lista_carteiras.mutex);
        return NULL;
    }

    lista_carteiras.num_carteiras++;

    pthread_mutex_unlock(&lista_carteiras.mutex);
    return nova_carteira;
}

static CarteiraInvestidor *obter_carteira(const char *id_investidor) {
    pthread_mutex_lock(&lista_carteiras.mutex);

    for (int i = 0; i < lista_carteiras.num_carteiras; i++) {
        if (strcmp(
                lista_carteiras.carteiras[i].id_investidor,
                id_investidor
            ) == 0) {
            CarteiraInvestidor *carteira =
                &lista_carteiras.carteiras[i];

            pthread_mutex_unlock(&lista_carteiras.mutex);
            return carteira;
        }
    }

    pthread_mutex_unlock(&lista_carteiras.mutex);
    return NULL;
}

static int enviar_resposta(const MensagemProtocolo *pedido, const MensagemProtocolo *resposta) {
    int fd_resposta = open(pedido->fifo_resposta, O_WRONLY);

    if (fd_resposta < 0) {
        perror("Corretora: erro ao abrir FIFO de resposta");
        return 0;
    }

    int sucesso = escrever_todos(
        fd_resposta,
        resposta,
        sizeof(*resposta)
    );

    close(fd_resposta);
    return sucesso;
}

static void preparar_resposta_base(const MensagemProtocolo *pedido, const char *id_corretora, MensagemProtocolo *resposta) {
    memset(resposta, 0, sizeof(*resposta));

    resposta->tipo = MSG_RESPOSTA_OK;

    copiar_texto(
        resposta->id_corretora,
        sizeof(resposta->id_corretora),
        id_corretora
    );

    copiar_texto(
        resposta->id_investidor,
        sizeof(resposta->id_investidor),
        pedido->id_investidor
    );
}

static void processar_pedido(const MensagemProtocolo *pedido, const char *id_corretora) {
    MensagemProtocolo resposta;
    preparar_resposta_base(pedido, id_corretora, &resposta);

    switch (pedido->tipo) {
        case MSG_LOGIN_INVESTIDOR:
            if (obter_ou_criar_carteira(
                    pedido->id_investidor
                ) == NULL) {
                resposta.tipo = MSG_RESPOSTA_ERRO;

                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Não foi possível criar a carteira."
                );
            } else {
                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Login efetuado com sucesso."
                );
            }

            /*
             * O investidor atual envia o login sem esperar resposta.
             * Por isso, não respondemos aqui.
             */
            return;

        case MSG_CONSULTAR_CARTEIRA: {
            CarteiraInvestidor *carteira =
                obter_carteira(pedido->id_investidor);

            if (carteira == NULL) {
                resposta.tipo = MSG_RESPOSTA_ERRO;

                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Carteira do investidor não encontrada."
                );
            } else {
                resposta.dados.carteira = *carteira;

                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Carteira consultada com sucesso."
                );
            }

            break;
        }

        case MSG_CONSULTAR_ORDENS: {
            CarteiraInvestidor *carteira =
                obter_carteira(pedido->id_investidor);

            if (carteira == NULL) {
                resposta.tipo = MSG_RESPOSTA_ERRO;

                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Carteira do investidor não encontrada."
                );
            } else {
                resposta.dados.carteira = *carteira;

                copiar_texto(
                    resposta.mensagem,
                    sizeof(resposta.mensagem),
                    "Histórico consultado com sucesso."
                );
            }

            break;
        }

        case MSG_LISTAR_COTACOES:
        case MSG_EMITIR_ORDEM:
            resposta.tipo = MSG_RESPOSTA_ERRO;

            copiar_texto(
                resposta.mensagem,
                sizeof(resposta.mensagem),
                "Operação ainda não ligada ao servidor da Bolsa."
            );
            break;

        case MSG_TERMINAR_SESSAO:
            printf(
                "Investidor '%s' terminou a sessão.\n",
                pedido->id_investidor
            );
            return;

        default:
            resposta.tipo = MSG_RESPOSTA_ERRO;

            copiar_texto(
                resposta.mensagem,
                sizeof(resposta.mensagem),
                "Tipo de mensagem desconhecido."
            );
            break;
    }

    if (!enviar_resposta(pedido, &resposta)) {
        fprintf(stderr, "Corretora: erro ao enviar resposta.\n");
    }
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(
            stderr,
            "Uso correto: %s <id_corretora>\n",
            argv[0]
        );
        return EXIT_FAILURE;
    }

    char id_corretora[MAX_NOME];

    if (!copiar_texto(
            id_corretora,
            sizeof(id_corretora),
            argv[1]
        )) {
        fprintf(stderr, "Erro: identificador demasiado comprido.\n");
        return EXIT_FAILURE;
    }

    char log_corretora[128];

    int tamanho = snprintf(
        log_corretora,
        sizeof(log_corretora),
        "eventos_corretora_%s.txt",
        id_corretora
    );

    if (tamanho < 0 ||
        (size_t) tamanho >= sizeof(log_corretora)) {
        fprintf(stderr, "Erro: nome do log demasiado comprido.\n");
        return EXIT_FAILURE;
    }

    char mensagem_log[160];

    snprintf(
        mensagem_log,
        sizeof(mensagem_log),
        "=== ARRANQUE DA CORRETORA: %s ===",
        id_corretora
    );

    gravar_log(log_corretora, mensagem_log);

    inicializar_corretora();

    char fifo_publico[128];

    tamanho = snprintf(
        fifo_publico,
        sizeof(fifo_publico),
        "%s%s",
        FIFO_CORRETORA_PREFIX,
        id_corretora
    );

    if (tamanho < 0 ||
        (size_t) tamanho >= sizeof(fifo_publico)) {
        fprintf(stderr, "Erro: caminho do FIFO demasiado longo.\n");
        gravar_log(
            log_corretora,
            "Erro: caminho do FIFO demasiado longo."
        );
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    unlink(fifo_publico);

    if (mkfifo(fifo_publico, 0666) < 0) {
        perror("Corretora: erro ao criar FIFO público");
        gravar_log(
            log_corretora,
            "Erro: não foi possível criar o FIFO público."
        );
        pthread_mutex_destroy(&lista_carteiras.mutex);
        return EXIT_FAILURE;
    }

    printf(
        "Corretora '%s' iniciada com sucesso.\n",
        id_corretora
    );

    printf(
        "A aguardar pedidos em: %s\n",
        fifo_publico
    );

    snprintf(
        mensagem_log,
        sizeof(mensagem_log),
        "FIFO público criado: %s",
        fifo_publico
    );

    gravar_log(log_corretora, mensagem_log);

    while (1) {
        int fd_fifo = open(fifo_publico, O_RDONLY);

        if (fd_fifo < 0) {
            perror("Corretora: erro ao abrir FIFO público");
            gravar_log(
                log_corretora,
                "Erro: não foi possível abrir o FIFO público."
            );
            break;
        }

        MensagemProtocolo pedido;

        while (ler_todos(
            fd_fifo,
            &pedido,
            sizeof(pedido)
        )) {
            printf(
                "[Corretora] Pedido tipo %d recebido de %s.\n",
                pedido.tipo,
                pedido.id_investidor
            );

            snprintf(
                mensagem_log,
                sizeof(mensagem_log),
                "Pedido tipo %d recebido do investidor '%s'.",
                pedido.tipo,
                pedido.id_investidor
            );

            gravar_log(log_corretora, mensagem_log);

            processar_pedido(
                &pedido,
                id_corretora
            );

            memset(&pedido, 0, sizeof(pedido));
        }

        close(fd_fifo);
    }

    unlink(fifo_publico);

    gravar_log(
        log_corretora,
        "=== ENCERRAMENTO DA CORRETORA ==="
    );

    pthread_mutex_destroy(&lista_carteiras.mutex);

    return EXIT_SUCCESS;
}