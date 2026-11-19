#include "bolsa.h"
#include <time.h>

void gravar_log(const char* nome_ficheiro_log, const char* mensagem) {
    static pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&mutex_log);

    FILE* log_file = fopen(nome_ficheiro_log, "a");
    if (log_file == NULL) {
        fprintf(stderr, "Erro ao aceder ao ficheiro de log: %s\n", nome_ficheiro_log);
        pthread_mutex_unlock(&mutex_log);
        return;
    }

    // Obter a hora atual
    time_t agora = time(NULL);
    struct tm tempo_local;
    char buffer[20];

    if (localtime_r(&agora, &tempo_local) == NULL ||
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tempo_local) == 0) {
        fprintf(stderr, "Erro ao obter timestamp para o log: %s\n", nome_ficheiro_log);
        fclose(log_file);
        pthread_mutex_unlock(&mutex_log);
        return;
    }

    if (fprintf(log_file, "[%s] %s\n", buffer, mensagem) < 0) {
        fprintf(stderr, "Erro ao escrever no ficheiro de log: %s\n", nome_ficheiro_log);
    }

    if (fclose(log_file) != 0) {
        fprintf(stderr, "Erro ao fechar o ficheiro de log: %s\n", nome_ficheiro_log);
    }

    pthread_mutex_unlock(&mutex_log);
}