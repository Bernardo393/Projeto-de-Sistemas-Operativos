CC = gcc
CFLAGS = -Wall -Wextra -g

# Nomes dos executáveis finais
TARGET_SERVIDOR = servidor_bolsa
TARGET_CLIENTE = cliente_bolsa

# A regra "all" agora gera os dois programas de uma vez
all: $(TARGET_SERVIDOR) $(TARGET_CLIENTE)

# Compila a Bolsa de Valores
$(TARGET_SERVIDOR): servidor.o logger.o config.o
	$(CC) $(CFLAGS) -o $(TARGET_SERVIDOR) servidor.o logger.o config.o

# Compila a Corretora
$(TARGET_CLIENTE): cliente.o logger.o
	$(CC) $(CFLAGS) -o $(TARGET_CLIENTE) cliente.o logger.o

# Regra genérica para converter ficheiros .c em ficheiros .o
%.o: %.c bolsa.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET_SERVIDOR) $(TARGET_CLIENTE) eventos_*.txt *.tmp