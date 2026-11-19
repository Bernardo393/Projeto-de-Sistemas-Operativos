# Projeto: Mercado de Valores Mobiliários

Projeto de Sistemas Operativos para gestão de empresas cotadas, corretoras e
investidores.

## Componentes

- `servidor_bolsa`: servidor TCP da Bolsa de Valores;
- `corretora`: gere carteiras locais e comunica com a Bolsa por TCP;
- `investidor`: interface do investidor e cliente FIFO da corretora.

O fluxo da segunda fase é:

```text
investidor --FIFO--> corretora --TCP--> servidor_bolsa
```

## Compilação

```bash
make clean
make
```

A compilação usa GCC com `-Wall -Wextra -g -pthread`.

## Execução

Inicia primeiro o servidor:

```bash
./servidor_bolsa
```

É possível indicar um ficheiro de configuração alternativo:

```bash
./servidor_bolsa outro_bvdata.txt
```

Noutro terminal inicia uma corretora:

```bash
./corretora ACIN
```

O endereço predefinido do servidor é `127.0.0.1` e a porta é `8080`. Também é
possível indicar outro endereço:

```bash
./corretora ACIN 127.0.0.1
```

Por fim, inicia um investidor:

```bash
./investidor ACIN ALBERTO
```

Podem ser executadas várias corretoras, desde que tenham identificadores
diferentes e não ultrapassem `MAX_CORRETORAS`.

## Comunicação

O investidor envia `PedidoInvestidor` pelo FIFO público da corretora. A
corretora responde com `RespostaCorretora` através de um FIFO privado criado
para cada investidor.

Os pedidos locais têm tamanho inferior a `PIPE_BUF`, garantindo escritas
atómicas no FIFO público. A comunicação entre corretora e Bolsa usa
`MensagemBolsaValores` através de um socket TCP.

## Funcionalidades implementadas

- carregamento e validação de `bvdata.txt`;
- gravação segura do estado do mercado;
- registo de eventos;
- registo TCP de corretoras;
- consulta de cotações;
- login de investidores;
- consulta de carteiras;
- consulta de ordens pendentes e transações;
- validação e encaminhamento de ordens;
- buffer produtor-consumidor para ordens pendentes;
- encerramento com sinais e limpeza dos FIFOs.

O matching completo, a atualização de carteiras após transações e as
notificações de execução ficam dependentes da fase definida no enunciado.

## Ficheiros gerados

Os objetos, executáveis, logs e ficheiros temporários são ignorados pelo Git.
Os FIFOs são criados em `/tmp` e removidos durante o encerramento normal.
