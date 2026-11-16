#include "bolsa.h"

int main(int argc, char *argv[]) {
    // 1. Validação dos parâmetros de configuração da Corretora
    if (argc < 2) {
        printf("Uso incorreto. Sintaxe: ./corretora <Nome_da_Corretora>\n");
        printf("Exemplo: ./corretora Corretora_Norte\n");
        return 1;
    }

    char nome_corretora[MAX_NOME];
    strncpy(nome_corretora, argv[1], MAX_NOME - 1);
    nome_corretora[MAX_NOME - 1] = '\0'; // Garantir terminação nula


    char log_corretora[128];
    snprintf(log_corretora, sizeof(log_corretora), "eventos_corretora_%s.txt", nome_corretora);

    // 2. Registo de Eventos do corretora
    char msg_arranque[100];
    snprintf(msg_arranque, sizeof(msg_arranque), "=== ARRANQUE DA CORRETORA: %s ===", nome_corretora);
    gravar_log(log_corretora, msg_arranque);

    printf("Corretora '%s' iniciada com sucesso.\n", nome_corretora);
    printf("A registar eventos em: %s\n", log_corretora);

    // (Futuro) Ponto de integração para a 2ª fase
    gravar_log(log_corretora, "Corretora iniciada. Comunicação com o servidor ainda não implementada.");

    // 3. Encerramento ordeiro
    gravar_log(log_corretora, "=== ENCERRAMENTO DA CORRETORA ===");

    return 0;
}