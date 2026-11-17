CC = gcc
CFLAGS = -Wall -Wextra -g 
LDLIBS = -pthread


# Nomes dos executáveis finais
TARGET_SERVIDOR = servidor_bolsa
TARGET_CORRETORA = corretora
TARGET_INVESTIDOR = investidor

OBJ_COMUNICACAO = comunicacao.o


all: $(TARGET_SERVIDOR) $(TARGET_CORRETORA) $(TARGET_INVESTIDOR)

# Compila a Bolsa de Valores
$(TARGET_SERVIDOR): servidor_bolsa.o logger.o config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Compila a Corretora
$(TARGET_CORRETORA): corretora.o logger.o $(OBJ_COMUNICACAO)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Compila o Investidor (Cliente interface)
$(TARGET_INVESTIDOR): investidor.o $(OBJ_COMUNICACAO)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Regra genérica para converter ficheiros .c em ficheiros .o
%.o: %.c bolsa.h comunicacao.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET_SERVIDOR) $(TARGET_CORRETORA) $(TARGET_INVESTIDOR) eventos_*.txt *.tmp /tmp/fifo_*