#ifndef BOLSA_H
#define BOLSA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EMPRESAS 50
#define MAX_NOME 30

typedef struct {
    char nome[MAX_NOME];
    int acoes_disponiveis;
    float valor_nominal;
    float ultima_cotacao;
} Empresa;

// Protótipos das funções que vamos criar
void gravar_log(const char* nome_ficheiro, const char* mensagem);
int carregar_empresas(const char* ficheiro, Empresa empresas[], int* num_empresas, const char *ficheiro_log);
void verificar_solucao(Empresa empresas[], int num_empresas, const char *ficheiro_log);

#endif