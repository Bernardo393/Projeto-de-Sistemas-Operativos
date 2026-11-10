#ifndef BOLSA_H
#define BOLSA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Limites do sistema
#define MAX_EMPRESAS 50
#define MAX_CORRETORAS 5
#define MAX_INVESTIDORES 20
#define MAX_NOME 30

// Limites relacionados com ordens e historico
#define MAX_ORDENS_BUFFER 20    // Número máximo de ordens que podem estar no buffer
#define MAX_ORDENS_NR 50        // Máximo de ordens não resolvidas por empresa
#define MAX_HISTORICO 30        // Registo histórico de ordens/transaçoes 

// Comunicação
#define PORTA_BV 8080           // Porta para comunicação com o servidor da bolsa de valores
#define IP_BV "127.0.0.1"       // Endereço IP do servidor da bolsa de valores
#define MAX_PATH 64             // Tamanho máximo do caminho para ficheiros

#define FIFO_CORRETORA_PREFIX "/tmp/fifo_corretora_"  // Prefixo para o nome do FIFO de cada corretora
#define FIFO_INVESTIDOR_PREFIX "/tmp/fifo_investidor_"      // Prefixo para o nome do FIFO de cada investidor

// Estruturas da Bolsa de Valores

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
    char corretora[MAX_NOME];
    char investidor[MAX_NOME];
    char nome_empresa[MAX_NOME];
    int quantidade;
    float preco_definitivo;
} NotificacaoOrdemCV;


// Estruturas de concorrência do servidor da bolsa de valores

/*  
    Tabela principal do mercado de ações 
    Permite varios leitores simultaneamente, mas apenas um escritor de cada vez.
*/
typedef struct {
    Empresa empresas[MAX_EMPRESAS];
    int num_empresas;
    pthread_rwlock_t lock; // Lock para proteger o acesso à tabela de empresas
} TabelaCotacoes;

/*
* Buffer circular de ordens:
* várias tarefas produtoras, uma por corretora,
* inserem ordens e a tarefa de matching consome-as.
*/
typedef struct {
    OrdemCV buffer[MAX_ORDENS_BUFFER];
    int in; 
    int out;    
    int count;                          // Número de ordens no buffer
    pthread_mutex_t mutex;              // Mutex para proteger o acesso ao buffer
    pthread_cond_t cond_nao_cheio;      // Condição para produtores (corretoras), bloqueia quando o buffer está cheio (count == MAX_ORDENS_BUFFER)
    pthread_cond_t cond_nao_vazio;      // Condição para consumidores (bolsa de valores), bloqueia quando o buffer está vazio (count == 0)
} BufferOrdensPendentes;

typedef struct {
    char nome_empresa[MAX_NOME];

    OrdemCV compras[MAX_ORDENS_NR];
    int num_compras;
    
    OrdemCV vendas[MAX_ORDENS_NR];
    int num_vendas;
} OrdensNaoResolvidasEmpresa;

typedef struct {
    char id_corretora[MAX_NOME];
    int fd_socket;      // Descritor do socket TCP da corretora
    int fd_pipe[2];     // Pipe interno (Matching -> Tarefa Dedicada)
    int ativa;          // 1 = ligada, 0 = slot livre
} DescritorCorretora;


// Estruturas da corretora e dos investidores

typedef struct {
    char nome_empresa[MAX_NOME];
    int quantidade;
} DescritorAcaoCarteira;

typedef struct {
    char id_investidor[MAX_NOME];

    DescritorAcaoCarteira acoes[MAX_EMPRESAS];
    int num_ativos;

    OrdemCV ordens_pendentes[MAX_HISTORICO];
    int num_ordens_pendentes;

    NotificacaoOrdemCV transacoes_realizadas[MAX_HISTORICO];
    int num_transacoes;
} CarteiraInvestidor;

typedef struct {
    CarteiraInvestidor carteiras[MAX_INVESTIDORES];
    int num_carteiras;

    pthread_mutex_t mutex; // Mutex para proteger o acesso à lista de carteiras
} ListaCarteiras;

// Protocolo de Comunicação

typedef enum {
    MSG_REGISTAR_CORRETORA = 1,
    MSG_LOGIN_INVESTIDOR,
    MSG_LISTAR_COTACOES,
    MSG_CONSULTAR_CARTEIRA,
    MSG_EMITIR_ORDEM,
    MSG_CONSULTAR_ORDENS,
    MSG_TERMINAR_SESSAO,
    MSG_NOTIFICACAO_TRANSACAO,
    MSG_RESPOSTA_OK,
    MSG_RESPOSTA_ERRO
} TipoMensagem;

typedef struct {
    TipoMensagem tipo;
    char id_corretora[MAX_NOME];
    char id_investidor[MAX_NOME];
    char fifo_resposta[MAX_PATH]; // FIFO para enviar a resposta de volta
    char mensagem[150]; 
    int num_elementos; 

    union {
        OrdemCV ordem;
        NotificacaoOrdemCV notificacao;
        Empresa cotacoes[MAX_EMPRESAS];
        CarteiraInvestidor carteira;
    } dados;
} MensagemProtocolo;



// Funções para manipulação de empresas
void gravar_log(const char* nome_ficheiro, const char* mensagem);
int carregar_empresas(const char* ficheiro, Empresa empresas[], int* num_empresas, const char *ficheiro_log);
int verificar_solucao(const Empresa empresas[], int num_empresas, const char *ficheiro_log);
int guardar_empresas(const char* ficheiro, const Empresa empresas[], int num_empresas, const char *ficheiro_log);

// Funções para manipulação de ordens de compra e venda
int validar_ordem(OrdemCV ordem, const Empresa empresas[], int num_empresas, const char *ficheiro_log);

#endif