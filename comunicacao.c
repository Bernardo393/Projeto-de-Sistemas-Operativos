#include "comunicacao.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int copiar_texto(char destino[], size_t tamanho, const char *origem) {
    int resultado;

    if (destino == NULL || origem == NULL || tamanho == 0) {
        return 0;
    }

    resultado = snprintf(destino, tamanho, "%s", origem);

    return resultado >= 0 && (size_t) resultado < tamanho;
}

int escrever_todos(int fd, const void *dados, size_t tamanho) {
    const char *ponteiro = dados;
    size_t total_escrito = 0;

    while (total_escrito < tamanho) {
        ssize_t resultado = write(fd, ponteiro + total_escrito, tamanho - total_escrito);

        if (resultado < 0) {
            if (errno == EINTR) {
                continue;
            }

            return 0;
        }

        if (resultado == 0) {
            return 0;
        }

        total_escrito += (size_t) resultado;
    }

    return 1;
}

int ler_todos(int fd, void *dados, size_t tamanho) {
    char *ponteiro = dados;
    size_t total_lido = 0;

    while (total_lido < tamanho) {
        ssize_t resultado = read(fd, ponteiro + total_lido, tamanho - total_lido);

        if (resultado < 0) {
            if (errno == EINTR) {
                continue;
            }

            return 0;
        }

        if (resultado == 0) {
            return 0;
        }

        total_lido += (size_t) resultado;
    }

    return 1;
}