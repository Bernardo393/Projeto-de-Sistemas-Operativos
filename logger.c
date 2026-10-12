#include "bolsa.h"
#include <time.h>

void gravar_log(const char* nome_ficheiro_log, const char* mensagem) {
    FILE* log_file = fopen(nome_ficheiro_log, "a");
    if (log_file == NULL) {
        fprintf(stderr, "Erro ao aceder ao ficheiro de log: %s\n", nome_ficheiro_log);
        return;
    }

    // Obter a hora atual
    time_t agora = time(NULL);
    struct tm* tempo_local = localtime(&agora);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tempo_local);

    fprintf(log_file, "[%s] %s\n", buffer, mensagem);
    fclose(log_file);
}