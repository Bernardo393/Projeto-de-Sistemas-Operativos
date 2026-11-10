#include "bolsa.h"


// Função auxiliar para verificar se uma empresa existe pelo nome
static int empresa_existe(const Empresa empresas[], int num_empresas, const char *nome) {
    for (int i = 0; i < num_empresas; i++) {
        if (strcmp(empresas[i].nome, nome) == 0) {
            return 1;
        }
    }
    return 0;
}

int verificar_solucao(const Empresa empresas[], int num_empresas, const char *ficheiro_log) {
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
    int erro_configuracao = 0;
    Empresa empresa_lida;

    while (*num_empresas < MAX_EMPRESAS && fgets(linha, sizeof(linha), file)) {
        numero_linha++;

        if (sscanf(linha, "%29s | %d | %f | %f",
            empresa_lida.nome,
            &empresa_lida.acoes_disponiveis,
            &empresa_lida.valor_nominal,
            &empresa_lida.ultima_cotacao) == 4) {

            if (empresa_existe(empresas, *num_empresas, empresa_lida.nome)) {
                char erro[180];

                snprintf(
                    erro,
                    sizeof(erro),
                    "Erro: Nome de empresa duplicado na linha %d: %s.",
                    numero_linha,
                    empresa_lida.nome
                );
                gravar_log(ficheiro_log, erro);
                erro_configuracao = 1;
                continue;
            }

            empresas[*num_empresas] = empresa_lida;
            (*num_empresas)++;
        } else {
            char erro[150];
            snprintf(erro, sizeof(erro), "Aviso: Formato inválido na linha %d do ficheiro de configuração.", numero_linha);
            gravar_log(ficheiro_log, erro);
        }
    }

    // Verifica se existem dados para além da capacidade suportada.
    if (*num_empresas == MAX_EMPRESAS) {
        while (fgets(linha, sizeof(linha), file) != NULL) {
            int apenas_espacos = 1;

            for (int i = 0; linha[i] != '\0'; i++) {
                if (linha[i] != ' ' && 
                    linha[i] != '\t' && 
                    linha[i] != '\n' && 
                    linha[i] != '\r') {
                    apenas_espacos = 0;
                    break;
                }
            }

            if (!apenas_espacos) {
                gravar_log(ficheiro_log, "Erro: o ficheiro contém mais empresas do que o limite suportado.");
                erro_configuracao = 1;
                break;
            }
        }
    }   

    fclose(file);

    if (erro_configuracao) {
        return 0;
    }
    
    char msg_sucesso[150];
    snprintf(msg_sucesso, sizeof(msg_sucesso), "Configuração carregada com sucesso: %d empresas lidas.", *num_empresas);
    gravar_log(ficheiro_log, msg_sucesso);

    return verificar_solucao(empresas, *num_empresas, ficheiro_log);
}

int guardar_empresas(const char *ficheiro, const Empresa empresas[], int num_empresas, const char *ficheiro_log) {
    char ficheiro_tmp[256];
    snprintf(ficheiro_tmp, sizeof(ficheiro_tmp), "%s.tmp", ficheiro);

    FILE *file = fopen(ficheiro_tmp, "w");
    if (file == NULL) {
        char erro[350];
        snprintf(erro, sizeof(erro), "Erro crítico: Falha ao criar o ficheiro temporário '%s'.", ficheiro_tmp);
        gravar_log(ficheiro_log, erro);
        return 0;
    }

    int em_escrita = 1;

    for (int i = 0; i < num_empresas; i++) {
        if(fprintf(file, "%s | %d | %.2f | %.2f\n", 
            empresas[i].nome,
            empresas[i].acoes_disponiveis,
            empresas[i].valor_nominal,
            empresas[i].ultima_cotacao) < 0) {
                em_escrita = 0;
                break;
            }
    }

    if (fclose(file) != 0 ) {
        em_escrita = 0;
    } 

    if (!em_escrita) {
        gravar_log(ficheiro_log, "Erro crítico: Falha ao escrever no ficheiro temporário.");
        remove(ficheiro_tmp);
        return 0;
    }

    if (rename(ficheiro_tmp, ficheiro) != 0) {
        char erro[350];
        snprintf(erro, sizeof(erro), "Erro crítico: Falha ao substituir '%s' pelo temporário.", ficheiro);        
        gravar_log(ficheiro_log, erro);
        remove(ficheiro_tmp);
        return 0;
    }

    char msg[350];
    snprintf(msg, sizeof(msg), "Configuração guardada com sucesso: %d empresas no ficheiro '%s'.", num_empresas, ficheiro);
    gravar_log(ficheiro_log, msg);
    return 1;
}

int validar_ordem(OrdemCV ordem, const Empresa empresas[], int num_empresas, const char *ficheiro_log) {
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

    
    if (!empresa_existe(empresas, num_empresas, ordem.nome_empresa)) {
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
