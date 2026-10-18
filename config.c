#include "bolsa.h"

int verificar_solucao(Empresa empresas[], int num_empresas, const char *ficheiro_log) {
    int erros = 0;
    char buffer_log[150];

    if (num_empresas <= 0) {
        printf("Erro: Nenhuma empresa válida foi carregada da configuração.\n");
        gravar_log(ficheiro_log, "Erro crítico: Ficheiro de configuração sem empresas válidas.");
        return 0;
    }

    for (int i = 0; i < num_empresas; i++) {
        const Empresa *empresa = &empresas[i];

        if (empresa->acoes_disponiveis < 0) {
            printf("Erro: A empresa '%s' tem ações disponíveis negativas (%d).\n", empresa->nome, empresa->acoes_disponiveis);
            snprintf(buffer_log, sizeof(buffer_log), "Erro: A empresa '%s' tem ações disponíveis negativas (%d).", empresa->nome, empresa->acoes_disponiveis);
            gravar_log(ficheiro_log, buffer_log);
            erros++;
        }
        if (empresa->valor_nominal <= 0.0f) {
            printf("Erro: A empresa '%s' tem valor nominal inválido (%.2f).\n", empresa->nome, empresa->valor_nominal);
            snprintf(buffer_log, sizeof(buffer_log), "Erro: A empresa '%s' tem valor nominal inválido (%.2f).", empresa->nome, empresa->valor_nominal);
            gravar_log(ficheiro_log, buffer_log);
            erros++;
        }
        if (empresa->ultima_cotacao <= 0.0f) {
            printf("Erro: A empresa '%s' tem última cotação inválida (%.2f).\n", empresa->nome, empresa->ultima_cotacao);
            snprintf(buffer_log, sizeof(buffer_log), "Erro: A empresa '%s' tem última cotação inválida (%.2f).", empresa->nome, empresa->ultima_cotacao);
            gravar_log(ficheiro_log, buffer_log);
            erros++;
        }
    }

    if (erros == 0) {
        printf("Verificação: solução correta.\n");
        gravar_log(ficheiro_log, "Verificação da configuração: solução correta.");
        return 1;
    } else {
        printf("Verificação: errada em %d números.\n", erros);
        snprintf(buffer_log, sizeof(buffer_log), "Verificação da configuração: errada em %d números.", erros);
        gravar_log(ficheiro_log, buffer_log);
        return 0;
    }
}

int carregar_empresas(const char* ficheiro, Empresa empresas[], int* num_empresas, const char *ficheiro_log) {
    FILE *file = fopen(ficheiro, "r");
    if (file == NULL) {
        char erro[150];
        snprintf(erro, sizeof(erro), "Erro crítico: Falha ao abrir o ficheiro de configuração '%s'.", ficheiro);
        gravar_log(ficheiro_log, erro);
        return 0;
    }

    char linha[256];
    *num_empresas = 0;
    int numero_linha = 0;

    while (*num_empresas < MAX_EMPRESAS && fgets(linha, sizeof(linha), file)) {
        numero_linha++;

        if (sscanf(linha, "%29s | %d | %f | %f", 
            empresas[*num_empresas].nome, 
            &empresas[*num_empresas].acoes_disponiveis, 
            &empresas[*num_empresas].valor_nominal, 
            &empresas[*num_empresas].ultima_cotacao) == 4) {
            
            (*num_empresas)++;
        } else {
            char erro[150];
            snprintf(erro, sizeof(erro), "Aviso: Formato inválido na linha %d do ficheiro de configuração.", numero_linha);
            gravar_log(ficheiro_log, erro);
        }
    }
    fclose(file);
    
    char msg_sucesso[150];
    snprintf(msg_sucesso, sizeof(msg_sucesso), "Configuração carregada com sucesso: %d empresas lidas.", *num_empresas);
    gravar_log(ficheiro_log, msg_sucesso);

    return verificar_solucao(empresas, *num_empresas, ficheiro_log);
}

int guardar_empresas(const char *ficheiro, Empresa empresas[], int num_empresas, const char *ficheiro_log) {
    char ficheiro_tmp[256];
    snprintf(ficheiro_tmp, sizeof(ficheiro_tmp), "%s.tmp", ficheiro);

    FILE *file = fopen(ficheiro_tmp, "w");
    if (file == NULL) {
        char erro[350];
        snprintf(erro, sizeof(erro), "Erro crítico: Falha ao criar o ficheiro temporário '%s'.", ficheiro_tmp);
        gravar_log(ficheiro_log, erro);
        return 0;
    }

    for (int i = 0; i < num_empresas; i++) {
        fprintf(file, "%s | %d | %.2f | %.2f\n", 
            empresas[i].nome,
            empresas[i].acoes_disponiveis,
            empresas[i].valor_nominal,
            empresas[i].ultima_cotacao);
    }

    fclose(file);

    if (rename(ficheiro_tmp, ficheiro) != 0) {
        char erro[350];
        snprintf(erro, sizeof(erro), "Erro crítico: Falha ao substituir '%s' pelo temporário.", ficheiro);        
        gravar_log(ficheiro_log, erro);
        return 0;
    }

    char msg[350];
    snprintf(msg, sizeof(msg), "Configuração guardada com sucesso: %d empresas no ficheiro '%s'.", num_empresas, ficheiro);
    gravar_log(ficheiro_log, msg);
    return 1;
}

int validar_ordem(OrdemCV ordem, Empresa empresas[], int num_empresas, const char *ficheiro_log) {
    char buffer_log[350];

    // Verifica se os campos obrigatórios estão preenchidos
    if (ordem.corretora[0] == '\0' || ordem.investidor[0] == '\0' || ordem.nome_empresa[0] == '\0') {
        gravar_log(ficheiro_log, "Erro: Ordem inválida - campos obrigatórios não preenchidos.");
        return 0;
    }
    
    // Verifica se o tipo de ordem é válido ('C' ou 'V')
    if (ordem.tipo != 'C' && ordem.tipo != 'V') {
        snprintf(buffer_log, sizeof(buffer_log), 
                 "Ordem INVÁLIDA [%s/%s]: Tipo de operação '%c' desconhecido.", 
                 ordem.corretora, ordem.investidor, ordem.tipo);
        gravar_log(ficheiro_log, buffer_log);
        return 0;
    }
    
    // Verifica se a quantidade e o preço são positivos
    if (ordem.quantidade <= 0 || ordem.preco <= 0.0f) {
        snprintf(buffer_log, sizeof(buffer_log), 
                 "Ordem INVÁLIDA [%s/%s | %s]: Valores incoerentes (Qtd: %d | Preço: %.2f).", 
                 ordem.corretora, ordem.investidor, ordem.nome_empresa, ordem.quantidade, ordem.preco);
        gravar_log(ficheiro_log, buffer_log);
        return 0;
    }

    // Verifica se a empresa existe em memória
    int empresa_encontrada = 0;
    for (int i = 0; i < num_empresas; i++) {
        if (strcmp(empresas[i].nome, ordem.nome_empresa) == 0) {
            empresa_encontrada = 1;
            break;
        }
    }
    
    if (!empresa_encontrada) {
        snprintf(buffer_log, sizeof(buffer_log), 
                 "Ordem INVÁLIDA [%s/%s]: Empresa '%s' não encontrada.", 
                 ordem.corretora, ordem.investidor, ordem.nome_empresa);
        gravar_log(ficheiro_log, buffer_log);
        return 0;
    }

    // Se todas as verificações passaram, a ordem é válida
    snprintf(buffer_log, sizeof(buffer_log), 
             "Ordem VÁLIDA [%s/%s]: %c | Empresa: %s | Qtd: %d | Preço: %.2f", 
             ordem.corretora, ordem.investidor, ordem.tipo, ordem.nome_empresa, ordem.quantidade, ordem.preco);
    gravar_log(ficheiro_log, buffer_log);

    return 1;
}