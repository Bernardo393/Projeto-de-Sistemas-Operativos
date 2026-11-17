#ifndef COMUNICACAO_H
#define COMUNICACAO_H

#include <stddef.h>

int copiar_texto(char destino[], size_t tamanho, const char *origem);
int escrever_todos(int fd, const void *dados, size_t tamanho);
int ler_todos(int fd, void *dados, size_t tamanho);

#endif