#include "bolsa.h"

int main() {
    Empresa mercado[MAX_EMPRESAS];    
    int total_empresas = 0;

    const char* log_servidor = "eventos_servidor.txt";

    gravar_log(log_servidor, "=== ARRANGUE DO SERVIDOR DA BOLSA ===");

    if (carregar_empresas("config.txt", mercado, &total_empresas, log_servidor)) {
        printf("\nServidor iniciado. Mercado aberto com %d empresas cotadas.\n", total_empresas);
        printf("--------------------------------------------------\n");
        
        for(int i = 0; i < total_empresas; i++) {
            printf("Cotada: %-15s | Ações: %-6d | Valor: %.2f\n", 
                   mercado[i].nome, mercado[i].acoes_disponiveis, mercado[i].valor_nominal);
        }
        printf("--------------------------------------------------\n");
        
    } else {
        printf("Falha fatal no arranque do servidor. Verifique os logs.\n");
        gravar_log(log_servidor, "=== ENCERRAMENTO POR FALHA CRÍTICA ===");
        return 1;
    } 

    printf("\nA guardar estado do mercado antes de encerrar...\n");

    // linha para testar a escrita no disco do ficheiro de configuração
    mercado[0].acoes_disponiveis -= 50;

    if (guardar_empresas("config.txt", mercado, total_empresas, log_servidor)) {
        printf("Estado do mercado guardado com sucesso.\n");
    } else {
        printf("Falha ao guardar o estado do mercado. Verifique os logs.\n");
    }

    gravar_log(log_servidor, "=== ENCERRAMENTO DO SERVIDOR ===");   
    return 0;
}

   