#define _WIN32_WINNT 0x0601
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <windows.h>

/*
    PROBLEMA 12 - FREQUÊNCIA DE PALAVRAS

    O programa possui duas formas de processamento:

    1. Sequencial:
       Uma única execução percorre todas as palavras.

    2. Paralela:
       As palavras são divididas entre várias threads.
       Cada thread possui sua própria tabela de frequência.
       No final, as tabelas locais são combinadas.

    Uso:

       frequencia.exe arquivo.txt 1
       frequencia.exe arquivo.txt 2
       frequencia.exe arquivo.txt 4
       frequencia.exe arquivo.txt 8
       frequencia.exe arquivo.txt max
*/


/* =========================================================
   ESTRUTURAS DE DADOS
   ========================================================= */

/*
    Estrutura que representa uma palavra na tabela.

    Exemplo:

        "casa" -> 15

    texto       = "casa"
    quantidade  = 15
    proxima     = próxima palavra da lista
*/
typedef struct Palavra {
    char *texto;
    long quantidade;
    struct Palavra *proxima;
} Palavra;


/*
    Tabela hash.

    Em vez de procurar uma palavra percorrendo
    todas as palavras existentes, usamos uma função
    hash para escolher uma posição da tabela.
*/
#define TAMANHO_TABELA 100003

typedef struct {
    Palavra *baldes[TAMANHO_TABELA];
} TabelaHash;


/*
    Estrutura utilizada pelas threads.

    Cada thread recebe:

    - o vetor completo de palavras;
    - a posição inicial;
    - a posição final;
    - uma tabela própria para armazenar suas frequências.
*/
typedef struct {
    char **palavras;

    long inicio;
    long fim;

    TabelaHash tabela_local;
} DadosThread;


/*
    Estrutura auxiliar utilizada para transformar
    a tabela hash em um vetor.

    Isso facilita a ordenação alfabética das palavras.
*/
typedef struct {
    char *texto;
    long quantidade;
} ItemResultado;


/* =========================================================
   FUNÇÃO DE TEMPO
   ========================================================= */

/*
    Retorna o tempo atual em segundos.

    No Windows usamos QueryPerformanceCounter,
    que fornece um contador de alta precisão.
*/
double tempo_atual() {

    static LARGE_INTEGER frequencia;
    LARGE_INTEGER contador;

    QueryPerformanceFrequency(&frequencia);
    QueryPerformanceCounter(&contador);

    return (double)contador.QuadPart /
           (double)frequencia.QuadPart;
}


/* =========================================================
   FUNÇÃO HASH
   ========================================================= */

/*
    Converte uma palavra em uma posição da tabela hash.

    Exemplo conceitual:

        "casa" -> posição 54821
        "carro" -> posição 81234

    Palavras diferentes podem eventualmente cair
    na mesma posição. Isso é chamado de colisão.

    A lista encadeada dentro de cada posição resolve
    essas colisões.
*/
unsigned long hash_string(const char *str) {

    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % TAMANHO_TABELA;
}


/* =========================================================
   TABELA HASH
   ========================================================= */

/*
    Inicializa a tabela.

    Todas as posições começam como NULL.
*/
void criar_tabela(TabelaHash *tabela) {

    for (int i = 0; i < TAMANHO_TABELA; i++) {
        tabela->baldes[i] = NULL;
    }
}


/*
    Procura uma palavra dentro da tabela.

    Se encontrar, retorna o endereço do elemento.

    Se não encontrar, retorna NULL.
*/
Palavra *buscar_palavra(TabelaHash *tabela,
                        const char *texto) {

    unsigned long indice = hash_string(texto);

    Palavra *atual = tabela->baldes[indice];

    while (atual != NULL) {

        if (strcmp(atual->texto, texto) == 0) {
            return atual;
        }

        atual = atual->proxima;
    }

    return NULL;
}


/*
    Adiciona uma ocorrência da palavra.

    Se a palavra já existir:
        quantidade++

    Se ainda não existir:
        cria uma nova entrada.
*/
void adicionar_palavra(TabelaHash *tabela,
                       const char *texto) {

    unsigned long indice = hash_string(texto);

    Palavra *atual = tabela->baldes[indice];

    /*
        Verifica se a palavra já existe.
    */
    while (atual != NULL) {

        if (strcmp(atual->texto, texto) == 0) {

            atual->quantidade++;

            return;
        }

        atual = atual->proxima;
    }

    /*
        Se chegou aqui, a palavra ainda não existe.
        Então criamos uma nova entrada.
    */
    Palavra *nova = malloc(sizeof(Palavra));

    if (nova == NULL) {
        printf("Erro ao alocar memoria.\n");
        exit(1);
    }

    nova->texto = malloc(strlen(texto) + 1);

    if (nova->texto == NULL) {
        printf("Erro ao alocar memoria.\n");
        exit(1);
    }

    strcpy(nova->texto, texto);

    nova->quantidade = 1;

    /*
        Insere no início da lista.
    */
    nova->proxima = tabela->baldes[indice];

    tabela->baldes[indice] = nova;
}


/*
    Adiciona várias ocorrências de uma vez.

    Essa função é usada durante a combinação das
    tabelas das threads.
*/
void adicionar_quantidade(TabelaHash *tabela,
                          const char *texto,
                          long quantidade) {

    Palavra *existente = buscar_palavra(tabela, texto);

    if (existente != NULL) {

        existente->quantidade += quantidade;

        return;
    }

    unsigned long indice = hash_string(texto);

    Palavra *nova = malloc(sizeof(Palavra));

    if (nova == NULL) {
        printf("Erro ao alocar memoria.\n");
        exit(1);
    }

    nova->texto = malloc(strlen(texto) + 1);

    if (nova->texto == NULL) {
        printf("Erro ao alocar memoria.\n");
        exit(1);
    }

    strcpy(nova->texto, texto);

    nova->quantidade = quantidade;

    nova->proxima = tabela->baldes[indice];

    tabela->baldes[indice] = nova;
}


/*
    Libera toda a memória utilizada pela tabela.
*/
void liberar_tabela(TabelaHash *tabela) {

    for (int i = 0; i < TAMANHO_TABELA; i++) {

        Palavra *atual = tabela->baldes[i];

        while (atual != NULL) {

            Palavra *proxima = atual->proxima;

            free(atual->texto);
            free(atual);

            atual = proxima;
        }

        tabela->baldes[i] = NULL;
    }
}


/* =========================================================
   LEITURA DO ARQUIVO
   ========================================================= */

/*
    Lê todas as palavras do arquivo e coloca em um vetor.

    O formato da atividade possui:

        primeira linha:
        quantidade aproximada de palavras

        depois:
        palavras separadas por espaços/quebras de linha.
*/
char **ler_arquivo(const char *nome_arquivo,
                   long *quantidade_palavras) {

    FILE *arquivo = fopen(nome_arquivo, "r");

    if (arquivo == NULL) {

        printf("Erro: nao foi possivel abrir o arquivo:\n");
        printf("%s\n", nome_arquivo);

        return NULL;
    }

    /*
        Ignora a primeira linha.

        Ela informa a quantidade aproximada de palavras.
    */
    char linha[1024];

    if (fgets(linha, sizeof(linha), arquivo) == NULL) {

        fclose(arquivo);

        return NULL;
    }

    /*
        Começamos com espaço para 1000 palavras.
    */
    long capacidade = 1000;

    char **palavras = malloc(
        capacidade * sizeof(char *)
    );

    if (palavras == NULL) {

        fclose(arquivo);

        return NULL;
    }

    long quantidade = 0;

    char buffer[1024];

    /*
        Lê cada token do arquivo.
    */
    while (fscanf(arquivo, "%1023s", buffer) == 1) {

        /*
            Se o vetor estiver cheio,
            dobramos sua capacidade.
        */
        if (quantidade >= capacidade) {

            capacidade *= 2;

            char **temporario = realloc(
                palavras,
                capacidade * sizeof(char *)
            );

            if (temporario == NULL) {

                printf("Erro ao aumentar vetor.\n");

                for (long i = 0; i < quantidade; i++) {
                    free(palavras[i]);
                }

                free(palavras);
                fclose(arquivo);

                return NULL;
            }

            palavras = temporario;
        }

        /*
            Aloca espaço para a palavra.
        */
        palavras[quantidade] =
            malloc(strlen(buffer) + 1);

        if (palavras[quantidade] == NULL) {

            printf("Erro ao alocar palavra.\n");

            for (long i = 0; i < quantidade; i++) {
                free(palavras[i]);
            }

            free(palavras);
            fclose(arquivo);

            return NULL;
        }

        strcpy(palavras[quantidade], buffer);

        quantidade++;
    }

    fclose(arquivo);

    *quantidade_palavras = quantidade;

    return palavras;
}


/*
    Libera o vetor de palavras.
*/
void liberar_palavras(char **palavras,
                      long quantidade) {

    for (long i = 0; i < quantidade; i++) {
        free(palavras[i]);
    }

    free(palavras);
}


/* =========================================================
   PROCESSAMENTO SEQUENCIAL
   ========================================================= */

/*
    Processa todas as palavras usando apenas
    uma execução.

    É a versão de referência do programa.
*/
void processar_sequencial(char **palavras,
                          long quantidade,
                          TabelaHash *tabela) {

    for (long i = 0; i < quantidade; i++) {

        adicionar_palavra(
            tabela,
            palavras[i]
        );
    }
}


/* =========================================================
   PROCESSAMENTO DA THREAD
   ========================================================= */

/*
    Cada thread executa esta função.

    Ela recebe uma parte do vetor.

    Exemplo:

        100 palavras
        4 threads

        Thread 0 -> 0 até 24
        Thread 1 -> 25 até 49
        Thread 2 -> 50 até 74
        Thread 3 -> 75 até 99
*/
void *processar_parte(void *argumento) {

    DadosThread *dados =
        (DadosThread *)argumento;

    criar_tabela(&dados->tabela_local);

    for (long i = dados->inicio;
         i < dados->fim;
         i++) {

        adicionar_palavra(
            &dados->tabela_local,
            dados->palavras[i]
        );
    }

    return NULL;
}


/* =========================================================
   COMBINAÇÃO DAS TABELAS
   ========================================================= */

/*
    Depois que todas as threads terminam,
    suas tabelas locais precisam ser combinadas.

    Exemplo:

        Thread 1:
        casa = 5

        Thread 2:
        casa = 3

        Thread 3:
        casa = 7

        Resultado:

        casa = 15
*/
void combinar_tabela(TabelaHash *destino,
                     TabelaHash *origem) {

    for (int i = 0; i < TAMANHO_TABELA; i++) {

        Palavra *atual =
            origem->baldes[i];

        while (atual != NULL) {

            adicionar_quantidade(
                destino,
                atual->texto,
                atual->quantidade
            );

            atual = atual->proxima;
        }
    }
}


/* =========================================================
   RESULTADOS
   ========================================================= */

/*
    Conta quantas palavras diferentes existem.
*/
long contar_distintas(TabelaHash *tabela) {

    long total = 0;

    for (int i = 0; i < TAMANHO_TABELA; i++) {

        Palavra *atual =
            tabela->baldes[i];

        while (atual != NULL) {

            total++;

            atual = atual->proxima;
        }
    }

    return total;
}


/*
    Copia a tabela para um vetor.
*/
ItemResultado *criar_lista_resultados(
    TabelaHash *tabela,
    long quantidade_distintas) {

    ItemResultado *lista =
        malloc(
            quantidade_distintas *
            sizeof(ItemResultado)
        );

    if (lista == NULL) {

        printf("Erro ao criar lista de resultados.\n");

        return NULL;
    }

    long posicao = 0;

    for (int i = 0; i < TAMANHO_TABELA; i++) {

        Palavra *atual =
            tabela->baldes[i];

        while (atual != NULL) {

            lista[posicao].texto =
                atual->texto;

            lista[posicao].quantidade =
                atual->quantidade;

            posicao++;

            atual = atual->proxima;
        }
    }

    return lista;
}


/*
    Função utilizada pelo qsort para ordenar
    as palavras alfabeticamente.
*/
int comparar_resultados(
    const void *a,
    const void *b) {

    const ItemResultado *x =
        (const ItemResultado *)a;

    const ItemResultado *y =
        (const ItemResultado *)b;

    return strcmp(x->texto, y->texto);
}


/*
    Mostra o resultado final.
*/
void imprimir_resultados(TabelaHash *tabela) {

    long quantidade_distintas =
        contar_distintas(tabela);

    ItemResultado *lista =
        criar_lista_resultados(
            tabela,
            quantidade_distintas
        );

    if (lista == NULL) {
        return;
    }

    qsort(
        lista,
        quantidade_distintas,
        sizeof(ItemResultado),
        comparar_resultados
    );

    printf("\n===== FREQUENCIA DE PALAVRAS =====\n");

    for (long i = 0;
         i < quantidade_distintas;
         i++) {

        printf(
            "%s : %ld\n",
            lista[i].texto,
            lista[i].quantidade
        );
    }

    printf(
        "\nTotal de palavras distintas: %ld\n",
        quantidade_distintas
    );

    free(lista);
}


/* =========================================================
   NUMERO DE PROCESSADORES
   ========================================================= */

/*
    Descobre quantos processadores lógicos
    estão disponíveis no Windows.
*/
int obter_max_threads() {

    DWORD quantidade =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS
        );

    if (quantidade == 0) {
        return 1;
    }

    return (int)quantidade;
}


/* =========================================================
   PROCESSAMENTO PARALELO
   ========================================================= */

void processar_paralelo(char **palavras,
                        long quantidade,
                        int numero_threads,
                        TabelaHash *resultado) {

    /*
        Vetor de threads.
    */
    pthread_t *threads =
        malloc(
            numero_threads *
            sizeof(pthread_t)
        );

    /*
        Informações de cada thread.
    */
    DadosThread *dados =
        malloc(
            numero_threads *
            sizeof(DadosThread)
        );

    if (threads == NULL || dados == NULL) {

        printf("Erro ao alocar memoria para threads.\n");

        free(threads);
        free(dados);

        return;
    }


    /*
        Calculamos quantas palavras cada thread
        deve receber.
    */
    long base =
        quantidade / numero_threads;

    /*
        Caso a divisão não seja exata,
        algumas threads receberão uma palavra
        adicional.
    */
    long resto =
        quantidade % numero_threads;


    long inicio = 0;


    /*
        Criamos as threads.
    */
    for (int i = 0;
         i < numero_threads;
         i++) {

        long tamanho =
            base;

        if (i < resto) {
            tamanho++;
        }

        dados[i].palavras = palavras;

        dados[i].inicio = inicio;

        dados[i].fim =
            inicio + tamanho;

        /*
            Cada thread possui sua própria tabela.
        */
        criar_tabela(
            &dados[i].tabela_local
        );


        int erro =
            pthread_create(
                &threads[i],
                NULL,
                processar_parte,
                &dados[i]
            );

        if (erro != 0) {

            printf(
                "Erro ao criar thread %d.\n",
                i
            );

            free(threads);
            free(dados);

            return;
        }

        inicio += tamanho;
    }


    /*
        Esperamos todas as threads terminarem.
    */
    for (int i = 0;
         i < numero_threads;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }


    /*
        Criamos a tabela final.
    */
    criar_tabela(resultado);


    /*
        Combinamos as tabelas locais.
    */
    for (int i = 0;
         i < numero_threads;
         i++) {

        combinar_tabela(
            resultado,
            &dados[i].tabela_local
        );

        liberar_tabela(
            &dados[i].tabela_local
        );
    }


    free(threads);
    free(dados);
}


/* =========================================================
   FUNÇÃO PRINCIPAL
   ========================================================= */

int main(int argc, char *argv[]) {

    /*
        Verifica se o usuário passou
        os argumentos necessários.

        Exemplo:

        frequencia.exe arquivo.txt 4
    */
    if (argc != 3) {

        printf("\nUso:\n");

        printf(
            "frequencia.exe <arquivo> <threads>\n"
        );

        printf("\nExemplos:\n");

        printf(
            "frequencia.exe entrada.txt 1\n"
        );

        printf(
            "frequencia.exe entrada.txt 2\n"
        );

        printf(
            "frequencia.exe entrada.txt 4\n"
        );

        printf(
            "frequencia.exe entrada.txt 8\n"
        );

        printf(
            "frequencia.exe entrada.txt max\n"
        );

        return 1;
    }


    /*
        Nome do arquivo.
    */
    const char *nome_arquivo =
        argv[1];


    /*
        Descobrimos o número máximo
        de processadores lógicos.
    */
    int max_threads =
        obter_max_threads();


    /*
        Converte o segundo argumento
        para número de threads.
    */
    int numero_threads;


    /*
        Se o usuário escreveu "max",
        usamos o máximo de CPUs lógicas.
    */
    if (strcmp(argv[2], "max") == 0) {

        numero_threads =
            max_threads;

    } else {

        numero_threads =
            atoi(argv[2]);
    }


    /*
        Verificação básica.
    */
    if (numero_threads < 1) {

        printf(
            "Numero de threads invalido.\n"
        );

        return 1;
    }


    /*
        Se o usuário pediu mais threads
        que o número de CPUs lógicas,
        permitimos mesmo assim.

        Isso pode ser útil para testes,
        embora a configuração "max"
        use exatamente o número detectado.
    */


    printf("\n====================================\n");

    printf(
        "PROBLEMA 12 - FREQUENCIA DE PALAVRAS\n"
    );

    printf("====================================\n");

    printf(
        "Arquivo: %s\n",
        nome_arquivo
    );

    printf(
        "CPUs logicas disponiveis: %d\n",
        max_threads
    );

    printf(
        "Threads utilizadas: %d\n",
        numero_threads
    );


    /*
        -----------------------------------------------------
        CARREGAMENTO DO ARQUIVO
        -----------------------------------------------------

        Importante:

        A leitura acontece antes da medição.

        Assim, tanto o sequencial quanto o paralelo
        trabalham com exatamente o mesmo vetor
        de palavras.
    */

    long quantidade_palavras;

    char **palavras =
        ler_arquivo(
            nome_arquivo,
            &quantidade_palavras
        );

    if (palavras == NULL) {

        return 1;
    }


    printf(
        "Palavras carregadas: %ld\n",
        quantidade_palavras
    );


    /*
        Tabela que armazenará o resultado.
    */
    TabelaHash resultado;


    /*
        -----------------------------------------------------
        INICIO DA MEDICAO
        -----------------------------------------------------
    */

    double inicio =
        tempo_atual();


    /*
        Se foi escolhida apenas 1 thread,
        executamos a versão sequencial.
    */
    if (numero_threads == 1) {

        criar_tabela(&resultado);

        processar_sequencial(
            palavras,
            quantidade_palavras,
            &resultado
        );

    } else {

        /*
            Caso contrário,
            executamos a versão paralela.
        */
        processar_paralelo(
            palavras,
            quantidade_palavras,
            numero_threads,
            &resultado
        );
    }


    /*
        -----------------------------------------------------
        FINAL DA MEDICAO
        -----------------------------------------------------
    */

    double fim =
        tempo_atual();


    double tempo =
        fim - inicio;


    /*
        Mostramos o resultado.
    */
    imprimir_resultados(
        &resultado
    );


    printf(
        "\nTempo de processamento: %.6f segundos\n",
        tempo
    );


    /*
        Libera memória.
    */
    liberar_tabela(
        &resultado
    );

    liberar_palavras(
        palavras,
        quantidade_palavras
    );


    printf("\nPrograma finalizado.\n");


    return 0;
}