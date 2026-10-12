CC = gcc
CFLAGS = -Wall -Wextra -g

# Nomes dos executáveis finais
TARGET_SERVIDOR = servidor_bolsa
TARGET_CLIENTE = cliente_bolsa

# Ficheiros objeto comuns que ambas as aplicações partilham
OBJS_COMUNS = logger.o config.o

# A regra "all" agora gera os dois programas de uma vez
all: $(TARGET_SERVIDOR) $(TARGET_CLIENTE)

# Compila a Bolsa de Valores
$(TARGET_SERVIDOR): servidor.o $(OBJS_COMUNS)
	$(CC) $(CFLAGS) -o $(TARGET_SERVIDOR) servidor.o $(OBJS_COMUNS)

# Compila a Corretora
$(TARGET_CLIENTE): cliente.o $(OBJS_COMUNS)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENTE) cliente.o $(OBJS_COMUNS)

# Regra genérica para converter ficheiros .c em ficheiros .o
%.o: %.c bolsa.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET_SERVIDOR) $(TARGET_CLIENTE) eventos_*.txt