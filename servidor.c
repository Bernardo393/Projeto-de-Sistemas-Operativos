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
    // Simulação de validação de ordens para demonstração da 1ª Fase
    printf("\n--- Teste de Validação de Ordens em Memória ---\n");
    
    OrdemCV ordens_teste[3] = {
        {'C', "Corretora_A", "Investidor_1", "Frisa", 100, 5.50f},       // Válida
        {'V', "Corretora_A", "Investidor_2", "EmpresaFalsa", 50, 4.00f}, // Inválida (empresa não existe)
        {'C', "Corretora_B", "Investidor_3", "Impor", -20, 5.00f}        // Inválida (quantidade negativa)
    };

    for (int i = 0; i < 3; i++) {
        if (validar_ordem(ordens_teste[i], mercado, total_empresas, log_servidor)) {
            printf("[SUCESSO] Ordem da empresa '%s' validada.\n", ordens_teste[i].nome_empresa);
        } else {
            printf("[ERRO] Ordem da empresa '%s' rejeitada (ver log).\n", ordens_teste[i].nome_empresa);
        }
    }

    if (guardar_empresas("config.txt", mercado, total_empresas, log_servidor)) {
        printf("Estado do mercado guardado com sucesso.\n");
    } else {
        printf("Falha ao guardar o estado do mercado. Verifique os logs.\n");
    }

    gravar_log(log_servidor, "=== ENCERRAMENTO DO SERVIDOR ===");   
    return 0;
}

   