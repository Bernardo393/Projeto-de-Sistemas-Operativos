CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

# Nomes dos executáveis finais
TARGET_SERVIDOR = servidor_bolsa
TARGET_CORRETORA = corretora
TARGET_INVESTIDOR = investidor

# A regra "all" agora gera os dois programas de uma vez
all: $(TARGET_SERVIDOR) $(TARGET_CORRETORA) $(TARGET_INVESTIDOR)

# Compila a Bolsa de Valores
$(TARGET_SERVIDOR): servidor_bolsa.o logger.o config.o
	$(CC) $(CFLAGS) -o $(TARGET_SERVIDOR) servidor_bolsa.o logger.o config.o

# Compila a Corretora
$(TARGET_CORRETORA): corretora.o logger.o
	$(CC) $(CFLAGS) -o $(TARGET_CORRETORA) corretora.o logger.o

# Compila o Investidor (Cliente interface)
$(TARGET_INVESTIDOR): investidor.o $(OBJS_COMUNS)
	$(CC) $(CFLAGS) -o $(TARGET_INVESTIDOR) investidor.o $(OBJS_COMUNS)

# Regra genérica para converter ficheiros .c em ficheiros .o
%.o: %.c bolsa.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET_SERVIDOR) $(TARGET_CORRETORA) $(TARGET_INVESTIDOR) eventos_*.txt *.tmp /tmp/fifo_*