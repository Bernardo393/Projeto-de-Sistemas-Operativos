#include "bolsa.h"

int main(int argc, char *argv[]) {
    // 1. Validação dos parâmetros de configuração da Corretora
    if (argc < 2) {
        printf("Uso incorreto. Sintaxe: ./cliente <Nome_da_Corretora>\n");
        printf("Exemplo: ./cliente Corretora_Norte\n");
        return 1;
    }

    char nome_corretora[MAX_NOME];
    strncpy(nome_corretora, argv[1], MAX_NOME - 1);
    nome_corretora[MAX_NOME - 1] = '\0'; // Garantir terminação nula


    char log_cliente[50];
    snprintf(log_cliente, sizeof(log_cliente), "eventos_cliente_%s.txt", nome_corretora);

    // 2. Registo de Eventos do Cliente
    char msg_arranque[100];
    snprintf(msg_arranque, sizeof(msg_arranque), "=== ARRANGUE DA CORRETORA: %s ===", nome_corretora);
    gravar_log(log_cliente, msg_arranque);

    printf("Corretora '%s' iniciada com sucesso.\n", nome_corretora);
    printf("A registar eventos em: %s\n", log_cliente);

    // (Futuro) Ponto de integração para a 2ª fase
    gravar_log(log_cliente, "Configuração carregada. A aguardar implementação da 2ª Fase (Sockets).");

    // 3. Encerramento ordeiro
    gravar_log(log_cliente, "=== ENCERRAMENTO DA CORRETORA ===");

    return 0;
}