#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

typedef struct Palavra {
    char *texto;
    long quantidade;
    struct Palavra *proxima;
} Palavra;

#define TAMANHO_TABELA 100003

typedef struct {
    Palavra *baldes[TAMANHO_TABELA];
} TabelaHash;

typedef struct {
    char **palavras;

    long inicio;
    long fim;

    TabelaHash tabela_local;
} DadosThread;

typedef struct {
    char *texto;
    long quantidade;
} ItemResultado;

double tempo_atual() {

    struct timespec instante; 

    if (clock_gettime(CLOCK_MONOTONIC, &instante) != 0) {
        perror("Erro ao consultar o relogio monotonic");
        exit(EXIT_FAILURE);
    }

    return (double)instante.tv_sec +
           (double)instante.tv_nsec / 1000000000.0;
}

unsigned long hash_string(const char *str) {

    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % TAMANHO_TABELA;
}

void criar_tabela(TabelaHash *tabela) {

    for (int i = 0; i < TAMANHO_TABELA; i++) {
        tabela->baldes[i] = NULL;
    }
}

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

void adicionar_palavra(TabelaHash *tabela,
                       const char *texto) {

    unsigned long indice = hash_string(texto);

    Palavra *atual = tabela->baldes[indice];

    while (atual != NULL) {

        if (strcmp(atual->texto, texto) == 0) {

            atual->quantidade++;

            return;
        }

        atual = atual->proxima;
    }

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

    nova->proxima = tabela->baldes[indice];

    tabela->baldes[indice] = nova;
}

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

char **ler_arquivo(const char *nome_arquivo,
                   long *quantidade_palavras) {

    FILE *arquivo = fopen(nome_arquivo, "r");

    if (arquivo == NULL) {

        printf("Erro: nao foi possivel abrir o arquivo:\n");
        printf("%s\n", nome_arquivo);

        return NULL;
    }

    char linha[1024];

    if (fgets(linha, sizeof(linha), arquivo) == NULL) {

        fclose(arquivo);

        return NULL;
    }

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

    while (fscanf(arquivo, "%1023s", buffer) == 1) {

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

void liberar_palavras(char **palavras,
                      long quantidade) {

    for (long i = 0; i < quantidade; i++) {
        free(palavras[i]);
    }

    free(palavras);
}

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

int comparar_resultados(
    const void *a,
    const void *b) {

    const ItemResultado *x =
        (const ItemResultado *)a;

    const ItemResultado *y =
        (const ItemResultado *)b;

    return strcmp(x->texto, y->texto);
}

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

int obter_max_threads() {

    long quantidade = sysconf(_SC_NPROCESSORS_ONLN);

    if (quantidade < 1) {
        return 1;
    }

    return (int)quantidade;
}

void processar_paralelo(char **palavras,
                        long quantidade,
                        int numero_threads,
                        TabelaHash *resultado) {

    pthread_t *threads =
        malloc(
            numero_threads *
            sizeof(pthread_t)
        );

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

    long base =
        quantidade / numero_threads;

    long resto =
        quantidade % numero_threads;


    long inicio = 0;

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

    for (int i = 0;
         i < numero_threads;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }

    criar_tabela(resultado);

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

int main(int argc, char *argv[]) {

    if (argc != 3) {

        printf("\nUso:\n");

        printf(
            "./frequencia_linux <arquivo> <threads>\n"
        );

        printf("\nExemplos:\n");

        printf(
            "./frequencia_linux entrada.txt 1\n"
        );

        printf(
            "./frequencia_linux entrada.txt 2\n"
        );

        printf(
            "./frequencia_linux entrada.txt 4\n"
        );

        printf(
            "./frequencia_linux entrada.txt 8\n"
        );

        printf(
            "./frequencia_linux entrada.txt max\n"
        );

        return 1;
    }

    const char *nome_arquivo =
        argv[1];

    int max_threads =
        obter_max_threads();

    int numero_threads;

    if (strcmp(argv[2], "max") == 0) {

        numero_threads =
            max_threads;

    } else {

        numero_threads =
            atoi(argv[2]);
    }

    if (numero_threads < 1) {

        printf(
            "Numero de threads invalido.\n"
        );

        return 1;
    }

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


    double inicio =
        tempo_atual();

    if (numero_threads == 1) {

        criar_tabela(&resultado);

        processar_sequencial(
            palavras,
            quantidade_palavras,
            &resultado
        );

    } else {

        processar_paralelo(
            palavras,
            quantidade_palavras,
            numero_threads,
            &resultado
        );
    }


    double fim =
        tempo_atual();


    double tempo =
        fim - inicio;


    imprimir_resultados(
        &resultado
    );


    printf(
        "\nTempo de processamento: %.6f segundos\n",
        tempo
    );


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