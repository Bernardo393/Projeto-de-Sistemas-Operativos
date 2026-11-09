#ifndef BOLSA_H
#define BOLSA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EMPRESAS 5
#define MAX_NOME 30

typedef struct {
    char nome[MAX_NOME];
    int acoes_disponiveis;
    float valor_nominal;
    float ultima_cotacao;
} Empresa;

typedef struct {
    char tipo;          // 'C' para compra, 'V' para venda
    char corretora[MAX_NOME];
    char investidor[MAX_NOME];
    char nome_empresa[MAX_NOME];
    int quantidade;
    float preco;
} OrdemCV;

typedef struct {
    char nome_empresa[MAX_NOME];
    int quantidade;
} DescritorAcao;

typedef struct {
    char id_investidor[MAX_NOME];
    DescritorAcao acoes[MAX_EMPRESAS];
    int num_ativos;
} CarteiraInvestidor;



// Funções para manipulação de empresas
void gravar_log(const char* nome_ficheiro, const char* mensagem);
int carregar_empresas(const char* ficheiro, Empresa empresas[], int* num_empresas, const char *ficheiro_log);
int verificar_solucao(const Empresa empresas[], int num_empresas, const char *ficheiro_log);
int guardar_empresas(const char* ficheiro, const Empresa empresas[], int num_empresas, const char *ficheiro_log);

// Funções para manipulação de ordens de compra e venda
int validar_ordem(OrdemCV ordem, const Empresa empresas[], int num_empresas, const char *ficheiro_log);

#endif