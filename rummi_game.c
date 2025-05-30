#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <limits.h>
#include <bits/time.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/poll.h>

// ----------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------
#define NUM_JUGADORES 4          // Número fijo de jugadores
#define MAX_FICHAS 108           // Total fichas en el mazo (standard para Rummy)
// Tiempo por turno en segundos (modificable)
int QUANTUM = 20;
#define PUNTOS_MINIMOS_APEADA 30 // Mínimo para primera apeada

#define FICHAS_INICIALES 14 // Fichas al repartir (puede variar según reglas)
#define MAX_COLOR 15        // Longitud máxima para nombre de color
#define MAX_NOMBRE 50       // Longitud máxima para nombre jugador

#define MAX_FICHAS_GRUPO 4     // Máximo fichas en grupo (ej: 4 sietes)
#define MAX_FICHAS_ESCALERA 13 // Máximo en escalera (A-2-...-K)
#define MAX_GRUPOS 10          // Máximo grupos en mesa
#define MAX_ESCALERAS 10       // Máximo escaleras en mesa
#define VALOR_COMODIN 0        // Valor numérico para comodines
#define MIN_FICHAS_GRUPO 3     // Mínimo fichas para un grupo
#define MIN_FICHAS_ESCALERA 3  // Mínimo fichas para escalera

#define TURNO_MAXIMO 30 // 30 segundos por turno

// ----------------------------------------------------------------------
// Estructuras y Tipos
// ----------------------------------------------------------------------

typedef struct
{
    int numero;            // Valor numérico de la ficha (1-13 para valores normales)
    char color[MAX_COLOR]; // Color de la ficha (ej: "rojo", "azul", "comodín")
} ficha_t;

typedef struct
{
    ficha_t *fichas; // Array dinámico de fichas
    int cantidad;    // Fichas actuales en mano
    int capacidad;   // Capacidad máxima actual del array
} mano_t;

typedef struct
{
    ficha_t fichas[MAX_FICHAS_GRUPO]; // Array estático para grupo (ej: 4 fichas igual número)
    int cantidad;                     // Fichas actuales en el grupo
} grupo_t;

typedef struct
{
    ficha_t fichas[MAX_FICHAS_ESCALERA]; // Array estático para escalera (ej: 3+ fichas consecutivas mismo color)
    int cantidad;                        // Fichas actuales en la escalera
} escalera_t;

typedef struct
{
    grupo_t *grupos;       // Array dinámico de grupos en mesa
    escalera_t *escaleras; // Array dinámico de escaleras en mesa
    int total_grupos;      // Grupos actuales
    int total_escaleras;   // Escaleras actuales
} banco_de_apeadas_t;

typedef struct
{
    mano_t mano;             // Fichas en mano del jugador
    char nombre[MAX_NOMBRE]; // Nombre del jugador
    int id;                  // Identificador único
    int tiempo_restante;     // Tiempo disponible (para turnos)
    int puntos_suficientes;  // Flag si alcanzó puntos mínimos
    bool en_juego;           // Estado activo/inactivo
    bool ficha_agregada;     // Nueva bandera: indica si ya agregó una ficha en el turno
} jugador_t;

typedef struct
{
    ficha_t fichas[MAX_FICHAS]; // Array estático con todas las fichas
    int cantidad;               // Fichas actuales en el mazo
} mazo_t;

typedef struct
{
    grupo_t *grupos;       // Array dinámico de grupos en mesa
    escalera_t *escaleras; // Array dinámico de escaleras en mesa
    int total_grupos;      // Grupos actuales
    int total_escaleras;   // Escaleras actuales
} apeada_t;

// Estados posibles de un jugador
typedef enum
{
    DE_ESPERA,
    LISTO,
    EJECUTANDO
} estado_jugador;

typedef struct
{
    int id_jugador;             // ID del jugador
    char nombre[MAX_NOMBRE];    // Nombre abreviado
    int fichas_en_mano;         // Fichas actuales
    int puntos;                 // Puntos acumulados
    int partidas_jugadas;       // Total partidas
    int partidas_ganadas;       // Victorias
    int partidas_perdidas;      // Derrotas
    estado_jugador estado;      // Estado actual
    int tiempo_total_juego;     // Tiempo jugado acumulado
    int turnos_jugados;         // Turnos tomados
    int fichas_robadas;         // Fichas robadas
    int fichas_desfichadas;     // Fichas desfichadas
    int grupos_formados;        // Grupos creados
    int escaleras_formadas;     // Escaleras creadas
    int apeadas_realizadas;    // <- Nuevo
    int embones_realizados;    // <- Nuevo
    int victorias_con_escalera; // Victorias con escalera completa
    int tiempo_restante;        // Tiempo en turno actual
    int tiempo_de_espera;       // Tiempo bloqueo
    
} pcb_t;

// Nuevo struct para pasar datos a los hilos
typedef struct
{
    volatile bool *terminar_flag;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
} hilo_control_t;

// ----------------------------------------------------------------------
// Variables Globales
// ----------------------------------------------------------------------
jugador_t jugadores[NUM_JUGADORES];                            // Array de jugadores
pcb_t pcbs[NUM_JUGADORES];                                     // Array de PCBs (estadísticas)
banco_de_apeadas_t banco_apeadas;                              // Banco de combinaciones (grupos/escaleras)
int turno_actual = 0;                                          // Turno actual (índice del jugador)
volatile bool juego_terminado = false;                         // Flag para terminar el juego
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;             // Mutex general
pthread_mutex_t mutex_terminacion = PTHREAD_MUTEX_INITIALIZER; // Mutex para la terminación del juego
pthread_mutex_t mutex_mesa = PTHREAD_MUTEX_INITIALIZER;        // Mutex para el banco
pthread_mutex_t mutex_juego = PTHREAD_MUTEX_INITIALIZER;       // Para estado general del juego
pthread_mutex_t mutex_colas = PTHREAD_MUTEX_INITIALIZER;       // Para colas de procesos
pthread_mutex_t mutex_pcbs = PTHREAD_MUTEX_INITIALIZER;        // Para estructuras PCB
pthread_mutex_t mutex_cola_listos = PTHREAD_MUTEX_INITIALIZER; // Para colas de listos

// Variables para el scheduler (planificador)
char modo = 'R';                                      // Modo de scheduling: 'F' (FCFS) o 'R' (Round Robin)
int cola_listos[NUM_JUGADORES];                       // Cola de jugadores listos para jugar
int frente = 0, final = 0;                            // Índices para la cola circular
int cola_de_esperas[NUM_JUGADORES];                   // Cola de jugadores de_esperas
int num_de_esperas = 0;                               // Contador de jugadores en espera
pthread_t hilo_juego;                                 // Hilo del juego
pthread_cond_t cond_turno = PTHREAD_COND_INITIALIZER; // Variable de condición para el juego
int proceso_en_ejecucion = -1;                        // ID del proceso en ejecución
int tiempo_restante_quantum = 0;                      // Tiempo restante del proceso en ejecución

mazo_t mazo;        // Mazo de fichas
int num_listos = 0; // Número de jugadores en la cola de listos

// ----------------------------------------------------------------------
// Funciones de inicializacion y liberacion
// ----------------------------------------------------------------------

// Prototipos de funciones
void mano_inicializar(mano_t *mano, int capacidad);
int kbhit();
void agregar_a_cola_listos(int id_jugador);
int siguiente_turno();
void reiniciar_cola_listos();
void *jugador_thread(void *arg);
bool puede_hacer_apeada(jugador_t *jugador);
int calcular_puntos_apeada(const apeada_t *apeada);
void verificar_cola_de_esperas();
bool es_grupo_valido(const ficha_t fichas[], int cantidad);
bool es_escalera_valida(const ficha_t fichas[], int cantidad);
void mostrar_robo_ficha(const ficha_t *ficha, bool es_automatico); // Nueva función

void mano_inicializar(mano_t *mano, int capacidad)
{
    if (mano == NULL || capacidad <= 0)
    {
        fprintf(stderr, "Error: Parámetros inválidos para inicializar mano\n");
        exit(EXIT_FAILURE);
    }

    mano->fichas = (ficha_t *)malloc(sizeof(ficha_t) * capacidad);
    if (mano->fichas == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para la mano\n");
        exit(EXIT_FAILURE);
    }

    mano->cantidad = 0;
    mano->capacidad = capacidad;
}

void inicializar_jugadores(mazo_t *mazo)
{
    if (mazo == NULL)
    {
        fprintf(stderr, "Error: Puntero a mazo inválido\n");
        exit(EXIT_FAILURE);
    }

    // Verificar suficientes fichas para todos los jugadores
    if (mazo->cantidad < FICHAS_INICIALES * NUM_JUGADORES)
    {
        fprintf(stderr, "Error: No hay suficientes fichas en el mazo (%d necesarias, %d disponibles)\n",
                FICHAS_INICIALES * NUM_JUGADORES, mazo->cantidad);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        // Inicializar datos básicos del jugador
        jugadores[i].id = i + 1; // IDs comienzan en 1
        snprintf(jugadores[i].nombre, MAX_NOMBRE, "Jugador %d", i + 1);
        jugadores[i].en_juego = true;
        jugadores[i].puntos_suficientes = false;
        jugadores[i].ficha_agregada = false;
        jugadores[i].tiempo_restante = QUANTUM * 2; // Tiempo inicial por jugador

        // Inicializar mano
        mano_inicializar(&jugadores[i].mano, FICHAS_INICIALES * 2); // Capacidad inicial doble

        // Repartir fichas (tomando del final del mazo)
        for (int j = 0; j < FICHAS_INICIALES; j++)
        {
            jugadores[i].mano.fichas[j] = mazo->fichas[mazo->cantidad - 1];
            mazo->cantidad--;
            jugadores[i].mano.cantidad++;
        }
    }
}

void banco_inicializar(banco_de_apeadas_t *banco)
{
    if (banco == NULL)
    {
        fprintf(stderr, "Error: Puntero a banco inválido\n");
        exit(EXIT_FAILURE);
    }

    banco->grupos = (grupo_t *)malloc(sizeof(grupo_t) * MAX_GRUPOS);
    if (banco->grupos == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para los grupos del banco\n");
        exit(EXIT_FAILURE);
    }

    banco->escaleras = (escalera_t *)malloc(sizeof(escalera_t) * MAX_ESCALERAS);
    if (banco->escaleras == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para las escaleras del banco\n");
        free(banco->grupos);
        exit(EXIT_FAILURE);
    }

    banco->total_grupos = 0;
    banco->total_escaleras = 0;

    // Inicializar cada grupo y escalera en el banco
    for (int i = 0; i < MAX_GRUPOS; i++)
    {
        banco->grupos[i].cantidad = 0;
        memset(banco->grupos[i].fichas, 0, sizeof(ficha_t) * MAX_FICHAS_GRUPO);
    }

    for (int i = 0; i < MAX_ESCALERAS; i++)
    {
        banco->escaleras[i].cantidad = 0;
        memset(banco->escaleras[i].fichas, 0, sizeof(ficha_t) * MAX_FICHAS_ESCALERA);
    }
}

void apeada_inicializar(apeada_t *apeada)
{
    if (apeada == NULL)
    {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        exit(EXIT_FAILURE);
    }

    apeada->grupos = (grupo_t *)malloc(sizeof(grupo_t) * MAX_GRUPOS);
    if (apeada->grupos == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para los grupos de apeada\n");
        exit(EXIT_FAILURE);
    }

    apeada->escaleras = (escalera_t *)malloc(sizeof(escalera_t) * MAX_ESCALERAS);
    if (apeada->escaleras == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para las escaleras de apeada\n");
        free(apeada->grupos);
        exit(EXIT_FAILURE);
    }

    apeada->total_grupos = 0;
    apeada->total_escaleras = 0;

    // Inicializar cada grupo y escalera en la apeada
    for (int i = 0; i < MAX_GRUPOS; i++)
    {
        apeada->grupos[i].cantidad = 0;
        memset(apeada->grupos[i].fichas, 0, sizeof(ficha_t) * MAX_FICHAS_GRUPO);
    }

    for (int i = 0; i < MAX_ESCALERAS; i++)
    {
        apeada->escaleras[i].cantidad = 0;
        memset(apeada->escaleras[i].fichas, 0, sizeof(ficha_t) * MAX_FICHAS_ESCALERA);
    }
}

void mano_liberar(mano_t *mano)
{
    if (mano != NULL)
    {
        free(mano->fichas);
        mano->fichas = NULL;
        mano->cantidad = 0;
        mano->capacidad = 0;
    }
}

void liberar_jugadores()
{
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        free(jugadores[i].mano.fichas); // Liberar memoria dinámica de las fichas
        jugadores[i].mano.fichas = NULL;
        jugadores[i].mano.cantidad = 0;
        jugadores[i].mano.capacidad = 0;
    }
}

void banco_liberar(banco_de_apeadas_t *banco)
{
    if (banco == NULL)
        return;

    if (banco->grupos != NULL)
    {
        free(banco->grupos);
        banco->grupos = NULL;
    }

    if (banco->escaleras != NULL)
    {
        free(banco->escaleras);
        banco->escaleras = NULL;
    }

    banco->total_grupos = 0;
    banco->total_escaleras = 0;
}

void apeada_liberar(apeada_t *apeada)
{
    if (apeada == NULL)
        return;

    if (apeada->grupos != NULL)
    {
        free(apeada->grupos);
        apeada->grupos = NULL;
    }

    if (apeada->escaleras != NULL)
    {
        free(apeada->escaleras);
        apeada->escaleras = NULL;
    }

    apeada->total_grupos = 0;
    apeada->total_escaleras = 0;
}

// ----------------------------------------------------------------------
// Funciones de puntuacion
// ----------------------------------------------------------------------

// Calcula puntos de un grupo
int calcular_puntos_grupo(const ficha_t grupo[], int cantidad)
{
    int valor_base = -1;
    for (int i = 0; i < cantidad; i++)
    {
        if (grupo[i].numero != 0)
        {
            valor_base = grupo[i].numero;
            break;
        }
    }
    if (valor_base == -1)
        return 0; // Si todas son comodines
    return valor_base * cantidad;
}

// Calcula puntos de una escalera
int calcular_puntos_escalera(const ficha_t escalera[], int cantidad)
{
    int puntos = 0;
    int max_val = 0;

    for (int i = 0; i < cantidad; i++)
    {
        if (escalera[i].numero != 0)
        {
            puntos += escalera[i].numero;
            if (escalera[i].numero > max_val)
            {
                max_val = escalera[i].numero;
            }
        }
    }

    // Asignar valor a comodines (valor máximo + 1 para cada uno)
    for (int i = 0; i < cantidad; i++)
    {
        if (escalera[i].numero == 0)
        {
            puntos += (max_val + 1);
            max_val++; // Incrementar para el próximo comodín
        }
    }

    return puntos;
}

// hacer_apeada
bool puede_hacer_apeada(jugador_t *jugador)
{
    // Verificar que el jugador tenga al menos 3 fichas
    if (jugador->mano.cantidad < 3)
    {
        return false; // No puede hacer apeada
    }

    // Intentar encontrar grupos válidos
    for (int i = 0; i < jugador->mano.cantidad - 2; i++)
    {
        for (int j = i + 1; j < jugador->mano.cantidad - 1; j++)
        {
            for (int k = j + 1; k < jugador->mano.cantidad; k++)
            {
                ficha_t grupo[3] = {jugador->mano.fichas[i], jugador->mano.fichas[j], jugador->mano.fichas[k]};
                if (es_grupo_valido(grupo, 3))
                {
                    return true; // Se encontró un grupo válido
                }
            }
        }
    }

    // Intentar encontrar escaleras válidas
    for (int i = 0; i < jugador->mano.cantidad - 2; i++)
    {
        for (int j = i + 1; j < jugador->mano.cantidad - 1; j++)
        {
            for (int k = j + 1; k < jugador->mano.cantidad; k++)
            {
                ficha_t escalera[3] = {jugador->mano.fichas[i], jugador->mano.fichas[j], jugador->mano.fichas[k]};
                if (es_escalera_valida(escalera, 3))
                {
                    return true; // Se encontró una escalera válida
                }
            }
        }
    }

    // Si no se encontraron grupos ni escaleras válidas
    return false;
}

// Calcula puntos totales de una apeada
int calcular_puntos_banco(const banco_de_apeadas_t *banco)
{
    int puntos = 0;

    for (int i = 0; i < banco->total_grupos; i++)
    {
        puntos += calcular_puntos_grupo(banco->grupos[i].fichas, banco->grupos[i].cantidad);
    }

    for (int i = 0; i < banco->total_escaleras; i++)
    {
        puntos += calcular_puntos_escalera(banco->escaleras[i].fichas, banco->escaleras[i].cantidad);
    }

    return puntos;
}

// Calcula puntos totales de una apeada
int calcular_puntos_apeada(const apeada_t *apeada)
{
    // Validación básica
    if (apeada == NULL)
    {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        return 0;
    }

    int puntos_totales = 0;

    // Sumar puntos de todos los grupos
    for (int i = 0; i < apeada->total_grupos; i++)
    {
        puntos_totales += calcular_puntos_grupo(apeada->grupos[i].fichas, apeada->grupos[i].cantidad);
    }

    // Sumar puntos de todas las escaleras
    for (int i = 0; i < apeada->total_escaleras; i++)
    {
        puntos_totales += calcular_puntos_escalera(apeada->escaleras[i].fichas, apeada->escaleras[i].cantidad);
    }

    return puntos_totales;
}

// ----------------------------------------------------------------------
// Funciones de logica de apeada
// ----------------------------------------------------------------------

// Verifica si un conjunto de fichas forma un grupo válido
bool es_grupo_valido(const ficha_t fichas[], int cantidad)
{
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_FICHAS_GRUPO)
    {
        return false;
    }

    int numero_base = -1;
    int comodines = 0;
    char colores_usados[MAX_FICHAS_GRUPO][MAX_COLOR] = {0};
    int colores_count = 0;

    for (int i = 0; i < cantidad; i++)
    {
        if (fichas[i].numero == VALOR_COMODIN)
        { // Usando VALOR_COMODIN
            comodines++;
        }
        else
        {
            // Verificar que no haya fichas duplicadas (mismo número y color)
            for (int j = 0; j < colores_count; j++)
            {
                if (strcmp(fichas[i].color, colores_usados[j]) == 0)
                {
                    return false; // Color repetido en el grupo
                }
            }

            // Guardar color usado
            strncpy(colores_usados[colores_count++], fichas[i].color, MAX_COLOR - 1);

            // Establecer o verificar número base
            if (numero_base == -1)
            {
                numero_base = fichas[i].numero;
            }
            else if (fichas[i].numero != numero_base)
            {
                return false; // Números diferentes
            }
        }
    }

    // Debe haber al menos una ficha no comodín
    return (numero_base != -1);
}

// Verifica si un conjunto de fichas forma una escalera válida
bool es_escalera_valida(const ficha_t fichas[], int cantidad)
{
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_FICHAS_ESCALERA)
    {
        return false;
    }

    int numeros[MAX_FICHAS_ESCALERA] = {0};
    char color_base[MAX_COLOR] = "";
    int idx = 0;
    int comodines = 0;

    // Procesar fichas y verificar color consistente
    for (int i = 0; i < cantidad; i++)
    {
        if (fichas[i].numero == VALOR_COMODIN)
        {
            comodines++;
        }
        else
        {
            // Verificar color consistente
            if (strlen(color_base) == 0)
            {
                strncpy(color_base, fichas[i].color, MAX_COLOR - 1);
            }
            else if (strcmp(color_base, fichas[i].color) != 0)
            {
                return false; // Diferente color
            }

            // Verificar que no haya números duplicados
            for (int j = 0; j < idx; j++)
            {
                if (numeros[j] == fichas[i].numero)
                {
                    return false; // Número duplicado
                }
            }

            numeros[idx++] = fichas[i].numero;
        }
    }

    // No puede ser solo comodines
    if (idx == 0)
        return false;

    // Ordenar números (usando bubble sort por simplicidad)
    for (int i = 0; i < idx - 1; i++)
    {
        for (int j = i + 1; j < idx; j++)
        {
            if (numeros[i] > numeros[j])
            {
                int temp = numeros[i];
                numeros[i] = numeros[j];
                numeros[j] = temp;
            }
        }
    }

    // Verificar secuencia con comodines
    int comodines_necesarios = 0;
    for (int i = 0; i < idx - 1; i++)
    {
        int diff = numeros[i + 1] - numeros[i];
        if (diff <= 0)
        {
            return false; // Números iguales o decrecientes
        }
        comodines_necesarios += (diff - 1);
    }

    // Comodines deben poder llenar todos los huecos
    if (comodines_necesarios > comodines)
    {
        return false;
    }

    // Comodines restantes solo pueden estar al inicio o final
    int comodines_restantes = comodines - comodines_necesarios;
    if (comodines_restantes > 0)
    {
        bool comodin_inicio = false;
        bool comodin_final = false;

        for (int i = 0; i < cantidad; i++)
        {
            if (fichas[i].numero == VALOR_COMODIN)
            {
                if (i > 0 && i < cantidad - 1)
                {
                    // Comodín en medio debe estar rellenando un hueco
                    if (comodines_restantes > 0)
                    {
                        return false;
                    }
                }
                else
                {
                    if (i == 0)
                        comodin_inicio = true;
                    if (i == cantidad - 1)
                        comodin_final = true;
                }
            }
        }
    }

    return true;
}

// Remueve una ficha de la mano en la posición especificada
void remover_ficha(mano_t *mano, int pos)
{
    if (pos >= 0 && pos < mano->cantidad)
    { // Verifica que la posición sea válida
        for (int i = pos; i < mano->cantidad - 1; i++)
        {
            mano->fichas[i] = mano->fichas[i + 1]; // Desplaza las fichas hacia la izquierda
        }
        mano->cantidad--; // Reduce la cantidad de fichas en la mano
    }
}

void remover_fichas(mano_t *mano, int indices[], int cantidad)
{
    // Ordenar indices de mayor a menor para evitar problemas
    for (int i = 0; i < cantidad - 1; i++)
    {
        for (int j = i + 1; j < cantidad; j++)
        {
            if (indices[i] < indices[j])
            {
                int temp = indices[i];
                indices[i] = indices[j];
                indices[j] = temp;
            }
        }
    }
    // Eliminar en orden descendente
    for (int i = 0; i < cantidad; i++)
    {
        remover_ficha(mano, indices[i]);
    }
}

// Busca el mejor grupo disponible en la mano y lo añade al banco
bool buscar_mejor_grupo(mano_t *mano, apeada_t *apeada, int *puntos)
{
    int mejor_puntos = 0;
    int mejores_indices[MAX_FICHAS_GRUPO] = {-1};
    int mejor_cantidad = 0;

    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                ficha_t grupo[3] = {mano->fichas[i], mano->fichas[j], mano->fichas[k]};

                if (es_grupo_valido(grupo, 3))
                {
                    int indices[4] = {i, j, k, -1};
                    int cantidad = 3;

                    // Verificar si podemos añadir una cuarta ficha
                    for (int l = k + 1; l < mano->cantidad; l++)
                    {
                        ficha_t grupo4[4] = {mano->fichas[i], mano->fichas[j],
                                             mano->fichas[k], mano->fichas[l]};
                        if (es_grupo_valido(grupo4, 4))
                        {
                            indices[3] = l;
                            cantidad = 4;
                            break;
                        }
                    }

                    // Calcular puntos
                    int pts = calcular_puntos_grupo(grupo, cantidad);
                    if (pts > mejor_puntos)
                    {
                        mejor_puntos = pts;
                        mejor_cantidad = cantidad;
                        memcpy(mejores_indices, indices, mejor_cantidad * sizeof(int));
                    }
                }
            }
        }
    }

    if (mejor_puntos > 0)
    {
        // Añadir el mejor grupo encontrado a la apeada
        if (apeada->total_grupos >= MAX_GRUPOS)
        {
            fprintf(stderr, "Error: Capacidad máxima de grupos alcanzada en la apeada\n");
            return false;
        }

        grupo_t *nuevo_grupo = &apeada->grupos[apeada->total_grupos++];
        nuevo_grupo->cantidad = mejor_cantidad;
        for (int i = 0; i < mejor_cantidad; i++)
        {
            nuevo_grupo->fichas[i] = mano->fichas[mejores_indices[i]];
        }

        *puntos += mejor_puntos;
        remover_fichas(mano, mejores_indices, mejor_cantidad);
        return true;
    }

    return false;
}

// Busca la mejor escalera disponible en la mano y la añade al banco
bool buscar_mejor_escalera(mano_t *mano, apeada_t *apeada, int *puntos)
{
    int mejor_puntos = 0;
    int mejores_indices[MAX_FICHAS_ESCALERA] = {-1};
    int mejor_cantidad = 0;

    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                ficha_t escalera[3] = {mano->fichas[i], mano->fichas[j], mano->fichas[k]};

                if (es_escalera_valida(escalera, 3))
                {
                    int indices[MAX_FICHAS_ESCALERA] = {i, j, k};
                    int cantidad = 3;

                    // Intentar extender la escalera
                    for (int l = 0; l < mano->cantidad; l++)
                    {
                        bool ya_en_escalera = false;
                        for (int m = 0; m < cantidad; m++)
                        {
                            if (l == indices[m])
                            {
                                ya_en_escalera = true;
                                break;
                            }
                        }

                        if (!ya_en_escalera)
                        {
                            ficha_t escalera_ext[MAX_FICHAS_ESCALERA];
                            for (int m = 0; m < cantidad; m++)
                            {
                                escalera_ext[m] = mano->fichas[indices[m]];
                            }
                            escalera_ext[cantidad] = mano->fichas[l];

                            if (es_escalera_valida(escalera_ext, cantidad + 1))
                            {
                                indices[cantidad] = l;
                                cantidad++;
                            }
                        }
                    }

                    // Calcular puntos
                    int pts = calcular_puntos_escalera(escalera, cantidad);
                    if (pts > mejor_puntos)
                    {
                        mejor_puntos = pts;
                        mejor_cantidad = cantidad;
                        memcpy(mejores_indices, indices, mejor_cantidad * sizeof(int));
                    }
                }
            }
        }
    }

    if (mejor_puntos > 0)
    {
        // Añadir la mejor escalera encontrada a la apeada
        if (apeada->total_escaleras >= MAX_ESCALERAS)
        {
            fprintf(stderr, "Error: Capacidad máxima de escaleras alcanzada en la apeada\n");
            return false;
        }

        escalera_t *nueva_escalera = &apeada->escaleras[apeada->total_escaleras++];
        nueva_escalera->cantidad = mejor_cantidad;
        for (int i = 0; i < mejor_cantidad; i++)
        {
            nueva_escalera->fichas[i] = mano->fichas[mejores_indices[i]];
        }

        *puntos += mejor_puntos;
        remover_fichas(mano, mejores_indices, mejor_cantidad);
        return true;
    }

    return false;
}

// Busca combinaciones según la prioridad especificada y las añade al banco
void buscar_combinaciones(mano_t *mano, apeada_t *apeada, int *puntos, bool priorizar_grupos)
{
    bool seguir_buscando = true;
    while (seguir_buscando)
    {
        bool encontrado = priorizar_grupos
                              ? buscar_mejor_grupo(mano, apeada, puntos) || buscar_mejor_escalera(mano, apeada, puntos)
                              : buscar_mejor_escalera(mano, apeada, puntos) || buscar_mejor_grupo(mano, apeada, puntos);

        seguir_buscando = encontrado;
    }
}

// Busca combinación mixta óptima y la añade al banco
void buscar_combinacion_mixta(mano_t *mano, apeada_t *apeada, int *puntos)
{
    // Primero buscamos grupos que usen números con múltiples fichas
    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        if (mano->fichas[i].numero == 0)
            continue;

        int contador = 1;
        for (int j = i + 1; j < mano->cantidad; j++)
        {
            if (mano->fichas[j].numero == mano->fichas[i].numero)
            {
                contador++;
            }
        }

        if (contador >= 2)
        {
            buscar_mejor_grupo(mano, apeada, puntos);
        }
    }

    // Luego buscamos escaleras con colores que tengan secuencias
    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        if (mano->fichas[i].numero == 0)
            continue;

        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            if (strcmp(mano->fichas[i].color, mano->fichas[j].color) == 0)
            {
                buscar_mejor_escalera(mano, apeada, puntos);
            }
        }
    }
}

// ----------------------------------------------------------------------
// Función principal para crear apeada
// ----------------------------------------------------------------------

// Función para mostrar la apeada realizada por el jugador
void mostrar_apeada(const apeada_t *apeada)
{
    // Validación de parámetros
    if (apeada == NULL)
    {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        return;
    }

    // Caso especial: apeada vacía
    if (apeada->total_grupos == 0 && apeada->total_escaleras == 0)
    {
        printf("\n=== No se realizaron combinaciones válidas ===\n");
        return;
    }

    // Encabezado informativo
    printf("\n=== Combinaciones de Apeada ===\n");

    // Mostrar grupos
    if (apeada->total_grupos > 0)
    {
        printf("\n── Grupos ──\n");
        for (int i = 0; i < apeada->total_grupos; i++)
        {
            printf("Grupo %d: ", i + 1);
            for (int j = 0; j < apeada->grupos[i].cantidad; j++)
            {
                if (apeada->grupos[i].fichas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", apeada->grupos[i].fichas[j].numero,
                           apeada->grupos[i].fichas[j].color);
                }
            }
            printf("\n");
        }
    }

    // Mostrar escaleras
    if (apeada->total_escaleras > 0)
    {
        printf("\n── Escaleras ──\n");
        for (int i = 0; i < apeada->total_escaleras; i++)
        {
            printf("Escalera %d: ", i + 1);
            for (int j = 0; j < apeada->escaleras[i].cantidad; j++)
            {
                if (apeada->escaleras[i].fichas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", apeada->escaleras[i].fichas[j].numero,
                           apeada->escaleras[i].fichas[j].color);
                }
            }
            printf("\n");
        }
    }

    // Mostrar resumen
    if (apeada->total_grupos > 0 || apeada->total_escaleras > 0)
    {
        printf("\nResumen:\n");
        printf("- Total grupos: %d\n", apeada->total_grupos);
        printf("- Total escaleras: %d\n", apeada->total_escaleras);
    }

    printf("────────────────────────────\n");
}

//Funcion para encontrar el índice de una ficha en la mano
static int encontrar_indice_ficha(const mano_t *mano, const ficha_t *ficha)
{
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (memcmp(&mano->fichas[i], ficha, sizeof(ficha_t)) == 0)
        {
            return i;
        }
    }
    return -1;
}

apeada_t calcular_mejor_apeada_aux(const jugador_t *jugador)
{
    apeada_t mejor_apeada;
    apeada_inicializar(&mejor_apeada);
    int max_puntos = 0;

    // Probar diferentes estrategias sin modificar la mano original
    for (int estrategia = 0; estrategia < 3; estrategia++)
    {
        // Crear copia temporal de la mano para pruebas
        mano_t mano_temp;
        mano_inicializar(&mano_temp, jugador->mano.capacidad);
        memcpy(mano_temp.fichas, jugador->mano.fichas, jugador->mano.cantidad * sizeof(ficha_t));
        mano_temp.cantidad = jugador->mano.cantidad;

        apeada_t apeada_temp;
        apeada_inicializar(&apeada_temp);
        int puntos_temp = 0;

        // Aplicar estrategia
        switch (estrategia)
        {
        case 0:
            buscar_combinaciones(&mano_temp, &apeada_temp, &puntos_temp, true);
            break;
        case 1:
            buscar_combinaciones(&mano_temp, &apeada_temp, &puntos_temp, false);
            break;
        case 2:
            buscar_combinacion_mixta(&mano_temp, &apeada_temp, &puntos_temp);
            break;
        }

        // Validar si cumple mínimo para primera apeada
        bool cumple_minimo = jugador->puntos_suficientes || puntos_temp >= PUNTOS_MINIMOS_APEADA;

        // Actualizar mejor apeada si corresponde
        if (puntos_temp > max_puntos && cumple_minimo)
        {
            apeada_liberar(&mejor_apeada);
            mejor_apeada = apeada_temp;
            max_puntos = puntos_temp;
        }
        else
        {
            apeada_liberar(&apeada_temp);
        }

        mano_liberar(&mano_temp);
    }

    return mejor_apeada;
}

apeada_t crear_mejor_apeada(jugador_t *jugador)
{
    apeada_t mejor_apeada;
    apeada_inicializar(&mejor_apeada);

    // Primero calcular sin modificar (versión de solo lectura)
    apeada_t apeada_calculada = calcular_mejor_apeada_aux(jugador);
    int puntos = calcular_puntos_apeada(&apeada_calculada);

    // Verificar si cumple mínimo para primera apeada
    bool apeada_valida = (jugador->puntos_suficientes || puntos >= PUNTOS_MINIMOS_APEADA) &&
                         (apeada_calculada.total_grupos > 0 || apeada_calculada.total_escaleras > 0);

    if (apeada_valida)
    {
        // Crear copia de la mano para modificar
        mano_t mano_temp;
        mano_inicializar(&mano_temp, jugador->mano.capacidad);
        memcpy(mano_temp.fichas, jugador->mano.fichas, jugador->mano.cantidad * sizeof(ficha_t));
        mano_temp.cantidad = jugador->mano.cantidad;

        // Eliminar fichas usadas en la apeada
        for (int g = 0; g < apeada_calculada.total_grupos; g++)
        {
            for (int c = 0; c < apeada_calculada.grupos[g].cantidad; c++)
            {
                int idx = -1;
                // Buscar la ficha en la mano (comparando todos los campos)
                for (int i = 0; i < mano_temp.cantidad; i++)
                {
                    if (memcmp(&mano_temp.fichas[i], &apeada_calculada.grupos[g].fichas[c], sizeof(ficha_t)) == 0)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx >= 0)
                {
                    remover_ficha(&mano_temp, idx);
                }
            }
        }

        // Actualizar mano real del jugador
        memcpy(jugador->mano.fichas, mano_temp.fichas, mano_temp.cantidad * sizeof(ficha_t));
        jugador->mano.cantidad = mano_temp.cantidad;
        mano_liberar(&mano_temp);

        // Transferir la apeada calculada a la de retorno
        mejor_apeada = apeada_calculada;
    }
    else
    {
        apeada_liberar(&apeada_calculada);
    }

    return mejor_apeada;
}

// Compara dos fichas para determinar si son iguales
bool son_fichas_iguales(const ficha_t ficha1, const ficha_t ficha2)
{
    // Comparar número y color
    return (ficha1.numero == ficha2.numero && strcmp(ficha1.color, ficha2.color) == 0);
}

void eliminar_ficha_de_mano(mano_t *mano, ficha_t ficha)
{
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (son_fichas_iguales(mano->fichas[i], ficha))
        {
            // Mover las fichas restantes una posición hacia atrás
            for (int j = i; j < mano->cantidad - 1; j++)
            {
                mano->fichas[j] = mano->fichas[j + 1];
            }
            mano->cantidad--;
            break;
        }
    }
}

bool realizar_apeada_optima(jugador_t *jugador, banco_de_apeadas_t *banco_mesa)
{
    if (!jugador->en_juego || jugador->mano.cantidad < 3)
    {
        printf("%s no puede realizar apeada (no está en juego o tiene muy pocas fichas).\n",
               jugador->nombre);
        return false;
    }

    apeada_t apeada_jugador = crear_mejor_apeada(jugador);
    int puntos_apeada = calcular_puntos_apeada(&apeada_jugador);

    // Validación estricta para primera apeada
    if (!jugador->puntos_suficientes)
    {
        if (puntos_apeada >= PUNTOS_MINIMOS_APEADA)
        {
            jugador->puntos_suficientes = true;
            printf("\n%s ha realizado su primera apeada con %d puntos (mínimo requerido: %d)!\n",
                   jugador->nombre, puntos_apeada, PUNTOS_MINIMOS_APEADA);
        }
        else
        {
            printf("\n%s no alcanzó el mínimo de %d puntos para la primera apeada.\n",
                   jugador->nombre, PUNTOS_MINIMOS_APEADA);
            apeada_liberar(&apeada_jugador);
            return false;
        }
    }
    else if (puntos_apeada == 0)
    {
        printf("\n%s no tiene combinaciones válidas para apear en este turno.\n", jugador->nombre);
        apeada_liberar(&apeada_jugador);
        return false;
    }

    // Eliminar fichas usadas en la apeada de la mano del jugador ---
    for (int i = 0; i < apeada_jugador.total_grupos; i++)
    {
        grupo_t *grupo = &apeada_jugador.grupos[i];
        for (int j = 0; j < grupo->cantidad; j++)
        {
            eliminar_ficha_de_mano(&jugador->mano, grupo->fichas[j]);
        }
    }

    for (int i = 0; i < apeada_jugador.total_escaleras; i++)
    {
        escalera_t *escalera = &apeada_jugador.escaleras[i];
        for (int j = 0; j < escalera->cantidad; j++)
        {
            eliminar_ficha_de_mano(&jugador->mano, escalera->fichas[j]);
        }
    }

    // Mostrar detalles de la apeada
    mostrar_apeada(&apeada_jugador);

    // Transferir grupos al banco
    for (int i = 0; i < apeada_jugador.total_grupos && banco_mesa->total_grupos < MAX_GRUPOS; i++)
    {
        banco_mesa->grupos[banco_mesa->total_grupos++] = apeada_jugador.grupos[i];
    }

    // Transferir escaleras al banco
    for (int i = 0; i < apeada_jugador.total_escaleras && banco_mesa->total_escaleras < MAX_ESCALERAS; i++)
    {
        banco_mesa->escaleras[banco_mesa->total_escaleras++] = apeada_jugador.escaleras[i];
    }

    // Actualizar estadísticas
    pcbs[jugador->id - 1].grupos_formados += apeada_jugador.total_grupos;
    pcbs[jugador->id - 1].escaleras_formadas += apeada_jugador.total_escaleras;
    pcbs[jugador->id - 1].puntos += puntos_apeada;

    // Registrar victoria con escalera si aplica
    if (apeada_jugador.total_escaleras > 0 && jugador->mano.cantidad == 0)
    {
        pcbs[jugador->id - 1].victorias_con_escalera++;
        printf("\n%s ha ganado usando al menos una escalera. ¡Se registra victoria con escalera!\n", jugador->nombre);
    }

    // Liberar recursos (solo estructuras, no las fichas transferidas)
    free(apeada_jugador.grupos);
    free(apeada_jugador.escaleras);

    return true;
}

// ----------------------------------------------------------------------
// Funciones para embonar
// ----------------------------------------------------------------------

// Verifica si una ficha puede ser agregada a un grupo existente
bool puede_embonar_grupo(const ficha_t *ficha, const grupo_t *grupo)
{
    // Si el grupo ya tiene el máximo de fichas
    if (grupo->cantidad >= MAX_FICHAS_GRUPO)
        return false;

    // Buscar el número base del grupo (ignorando comodines)
    int numero_base = -1;
    for (int i = 0; i < grupo->cantidad; i++)
    {
        if (grupo->fichas[i].numero != 0)
        {
            numero_base = grupo->fichas[i].numero;
            break;
        }
    }

    // Si todas son comodines, cualquier ficha puede unirse
    if (numero_base == -1)
        return true;

    // La ficha debe coincidir con el número base o ser comodín
    return (ficha->numero == numero_base || ficha->numero == 0);
}

// Verifica si una ficha puede ser agregada a una escalera existente
bool puede_embonar_escalera(const ficha_t *ficha, const escalera_t *escalera)
{
    // Si la escalera ya tiene el máximo de fichas
    if (escalera->cantidad >= MAX_FICHAS_ESCALERA)
        return false;

    // Buscar color base y valores existentes
    char color_base[MAX_COLOR] = "";
    int valores[MAX_FICHAS_ESCALERA];
    int num_valores = 0;
    int comodines = 0;

    for (int i = 0; i < escalera->cantidad; i++)
    {
        if (escalera->fichas[i].numero == 0)
        {
            comodines++;
        }
        else
        {
            if (strlen(color_base) == 0)
            {
                strcpy(color_base, escalera->fichas[i].color);
            }
            valores[num_valores++] = escalera->fichas[i].numero;
        }
    }

    // Si no hay fichas no-comodín, cualquier ficha del mismo color puede unirse
    if (num_valores == 0)
    {
        return (strcmp(ficha->color, color_base) == 0 || strlen(color_base) == 0);
    }

    // Verificar color
    if (strcmp(ficha->color, color_base) != 0 && ficha->numero != 0)
    {
        return false;
    }

    // Ordenar valores
    for (int i = 0; i < num_valores - 1; i++)
    {
        for (int j = i + 1; j < num_valores; j++)
        {
            if (valores[i] > valores[j])
            {
                int temp = valores[i];
                valores[i] = valores[j];
                valores[j] = temp;
            }
        }
    }

    // Verificar si la ficha puede encajar en la secuencia
    if (ficha->numero != 0)
    { // No es comodín
        // Verificar si el número ya existe
        for (int i = 0; i < num_valores; i++)
        {
            if (valores[i] == ficha->numero)
            {
                return false;
            }
        }

        // Verificar extremos
        if (ficha->numero == valores[0] - 1 || ficha->numero == valores[num_valores - 1] + 1)
        {
            return true;
        }

        // Verificar huecos internos
        for (int i = 0; i < num_valores - 1; i++)
        {
            if (ficha->numero > valores[i] && ficha->numero < valores[i + 1])
            {
                return true;
            }
        }
    }
    else
    {                // Es comodín
        return true; // Comodín siempre puede agregarse
    }

    return false;
}

// Intenta mover un comodín de un grupo a otro lugar para liberar espacio
bool mover_comodin_para_embonar(banco_de_apeadas_t *banco, const ficha_t *ficha)
{
    // Buscar comodines en grupos
    for (int g = 0; g < banco->total_grupos; g++)
    {
        grupo_t *grupo = &banco->grupos[g];

        // Buscar comodines en este grupo
        for (int i = 0; i < grupo->cantidad; i++)
        {
            if (grupo->fichas[i].numero == 0)
            { // Es comodín
                // Intentar mover este comodín a una escalera
                for (int e = 0; e < banco->total_escaleras; e++)
                {
                    escalera_t *escalera = &banco->escaleras[e];

                    if (escalera->cantidad < MAX_FICHAS_ESCALERA)
                    {
                        // Mover comodín del grupo a la escalera
                        ficha_t comodin = grupo->fichas[i];

                        // Eliminar comodín del grupo
                        for (int j = i; j < grupo->cantidad - 1; j++)
                        {
                            grupo->fichas[j] = grupo->fichas[j + 1];
                        }
                        grupo->cantidad--;

                        // Agregar comodín a la escalera
                        escalera->fichas[escalera->cantidad++] = comodin;

                        // Ahora intentar embonar la ficha original en el grupo
                        if (puede_embonar_grupo(ficha, grupo))
                        {
                            return true;
                        }

                        // Si no funciona, revertir el movimiento
                        // Devolver comodín al grupo
                        grupo->fichas[grupo->cantidad++] = comodin;
                        // Quitar de la escalera
                        escalera->cantidad--;

                        break;
                    }
                }
            }
        }
    }

    // Buscar comodines en escaleras
    for (int e = 0; e < banco->total_escaleras; e++)
    {
        escalera_t *escalera = &banco->escaleras[e];

        // Buscar comodines en esta escalera
        for (int i = 0; i < escalera->cantidad; i++)
        {
            if (escalera->fichas[i].numero == 0)
            { // Es comodín
                // Intentar mover este comodín a un grupo
                for (int g = 0; g < banco->total_grupos; g++)
                {
                    grupo_t *grupo = &banco->grupos[g];

                    if (grupo->cantidad < MAX_FICHAS_GRUPO)
                    {
                        // Mover comodín de la escalera al grupo
                        ficha_t comodin = escalera->fichas[i];

                        // Eliminar comodín de la escalera
                        for (int j = i; j < escalera->cantidad - 1; j++)
                        {
                            escalera->fichas[j] = escalera->fichas[j + 1];
                        }
                        escalera->cantidad--;

                        // Agregar comodín al grupo
                        grupo->fichas[grupo->cantidad++] = comodin;

                        // Ahora intentar embonar la ficha original en la escalera
                        if (puede_embonar_escalera(ficha, escalera))
                        {
                            return true;
                        }

                        // Si no funciona, revertir el movimiento
                        // Devolver comodín a la escalera
                        escalera->fichas[escalera->cantidad++] = comodin;
                        // Quitar del grupo
                        grupo->cantidad--;

                        break;
                    }
                }
            }
        }
    }

    return false;
}

// Función para intentar embonar una ficha en un grupo o escalera
bool intentar_embonar_ficha(ficha_t *ficha, banco_de_apeadas_t *banco)
{
    // Intentar embonar en grupos
    for (int i = 0; i < banco->total_grupos; i++)
    {
        if (puede_embonar_grupo(ficha, &banco->grupos[i]))
        {
            banco->grupos[i].fichas[banco->grupos[i].cantidad++] = *ficha;
            return true;
        }
    }

    // Intentar embonar en escaleras
    for (int i = 0; i < banco->total_escaleras; i++)
    {
        if (puede_embonar_escalera(ficha, &banco->escaleras[i]))
        {
            banco->escaleras[i].fichas[banco->escaleras[i].cantidad++] = *ficha;
            return true;
        }
    }

    // Si no se pudo embonar, intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(ficha, 1))
    {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->fichas[0] = *ficha;
        nuevo_grupo->cantidad = 1;
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(ficha, 1))
    {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->fichas[0] = *ficha;
        nueva_escalera->cantidad = 1;
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Función para intentar mover comodines y luego embonar la ficha
bool intentar_mover_comodin_y_embonar(ficha_t *ficha, banco_de_apeadas_t *banco)
{
    if (mover_comodin_para_embonar(banco, ficha))
    {
        return intentar_embonar_ficha(ficha, banco);
    }
    return false;
}

// Función principal para embonar una ficha del jugador al banco
bool embonar_ficha(jugador_t *jugador, banco_de_apeadas_t *banco, int indice_ficha)
{
    if (indice_ficha < 0 || indice_ficha >= jugador->mano.cantidad)
    {
        return false; // Índice inválido
    }

    ficha_t ficha = jugador->mano.fichas[indice_ficha];

    // Intentar embonar directamente
    if (intentar_embonar_ficha(&ficha, banco))
    {
        remover_ficha(&jugador->mano, indice_ficha);
        return true;
    }

    // Intentar mover comodines y luego embonar
    if (intentar_mover_comodin_y_embonar(&ficha, banco))
    {
        remover_ficha(&jugador->mano, indice_ficha);
        return true;
    }

    // Intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(&ficha, 1))
    {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->fichas[0] = ficha;
        nuevo_grupo->cantidad = 1;
        remover_ficha(&jugador->mano, indice_ficha);
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(&ficha, 1))
    {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->fichas[0] = ficha;
        nueva_escalera->cantidad = 1;
        remover_ficha(&jugador->mano, indice_ficha);
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Verifica si una ficha puede embonar en algún grupo o escalera del banco
bool existe_embon_posible_aux(const jugador_t *jugador, const banco_de_apeadas_t *banco)
{
    // 1. Verificar embón en grupos/escaleras existentes
    for (int i = 0; i < jugador->mano.cantidad; i++)
    {
        const ficha_t *ficha = &jugador->mano.fichas[i];

        // Verificar grupos
        for (int g = 0; g < banco->total_grupos; g++)
        {
            if (puede_embonar_grupo(ficha, &banco->grupos[g]))
            {
                return true;
            }
        }

        // Verificar escaleras
        for (int e = 0; e < banco->total_escaleras; e++)
        {
            if (puede_embonar_escalera(ficha, &banco->escaleras[e]))
            {
                return true;
            }
        }
    }

    // 2. Verificar si puede crear nuevos grupos/escaleras (solo si hay espacio)
    if (banco->total_grupos < MAX_GRUPOS || banco->total_escaleras < MAX_ESCALERAS)
    {
        for (int i = 0; i < jugador->mano.cantidad; i++)
        {
            const ficha_t *ficha = &jugador->mano.fichas[i];

            // Crear nuevo grupo (necesita al menos 2 fichas iguales + comodines)
            if (banco->total_grupos < MAX_GRUPOS)
            {
                int contador = 0;
                for (int j = 0; j < jugador->mano.cantidad; j++)
                {
                    if (i != j && (jugador->mano.fichas[j].numero == ficha->numero ||
                                   jugador->mano.fichas[j].numero == 0))
                    {
                        contador++;
                        if (contador >= 2)
                            return true; // 2 fichas iguales o 1 + comodín
                    }
                }
            }

            // Crear nueva escalera (necesita fichas consecutivas del mismo color)
            if (banco->total_escaleras < MAX_ESCALERAS)
            {
                for (int j = 0; j < jugador->mano.cantidad; j++)
                {
                    if (i != j && strcmp(jugador->mano.fichas[j].color, ficha->color) == 0)
                    {
                        int diff = abs(jugador->mano.fichas[j].numero - ficha->numero);
                        if (diff <= 2 || diff == 12)
                        { // Ej: Q-K-A o 2-3-4
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

// ----------------------------------------------------------------------
// Funciones para determinar un ganador
// ----------------------------------------------------------------------

// Calcula los puntos totales en la mano de un jugador
int calcular_puntos_mano(const mano_t *mano)
{
    int puntos = 0;
    for (int i = 0; i < mano->cantidad; i++)
    {
        // Comodines valen 20 puntos
        if (mano->fichas[i].numero == 0)
        {
            puntos += 20;
        }
        // Fichas normales valen su valor numérico
        else
        {
            puntos += mano->fichas[i].numero;
        }
    }
    return puntos;
}

// Verifica si un jugador ha ganado (se quedó sin fichas)
bool jugador_ha_ganado(const jugador_t *jugador)
{
    return jugador->mano.cantidad == 0;
}

// Determina el índice del jugador ganador
int determinar_ganador(jugador_t *jugadores, int num_jugadores, bool mazo_vacio)
{
    // Primero verificar si algún jugador se quedó sin fichas
    for (int i = 0; i < num_jugadores; i++)
    {
        if (jugador_ha_ganado(&jugadores[i]))
        {
            return i;
        }
    }

    // Si el mazo está vacío, determinar por menor puntaje
    if (mazo_vacio)
    {
        int indice_ganador = 0;
        int menor_puntaje = calcular_puntos_mano(&jugadores[0].mano);

        for (int i = 1; i < num_jugadores; i++)
        {
            int puntaje_actual = calcular_puntos_mano(&jugadores[i].mano);
            if (puntaje_actual < menor_puntaje)
            {
                menor_puntaje = puntaje_actual;
                indice_ganador = i;
            }
        }
        return indice_ganador;
    }

    // Si no hay ganador aún
    return -1;
}

// ----------------------------------------------------------------------
// Funciones para el Mazo
// ----------------------------------------------------------------------
void inicializar_mazo(mazo_t *mazo)
{
    const char *colores[] = {"rojo", "negro", "azul", "amarillo"};
    int index = 0;

    // Generar las 104 fichas normales (2 juegos de 1-13 en 4 colores)
    for (int k = 0; k < 2; k++)
    {
        for (int c = 0; c < 4; c++)
        {
            for (int n = 1; n <= 13; n++)
            {
                mazo->fichas[index].numero = n;
                strcpy(mazo->fichas[index].color, colores[c]);
                index++;
            }
        }
    }

    // Agregar los 4 comodines
    for (int j = 0; j < 4; j++)
    {
        mazo->fichas[index].numero = 0;
        strcpy(mazo->fichas[index].color, "comodin");
        index++;
    }

    mazo->cantidad = index;

    // Validar que el mazo tenga exactamente 108 fichas
    if (mazo->cantidad != MAX_FICHAS)
    {
        fprintf(stderr, "Error: El mazo no tiene 108 fichas. Tiene %d fichas.\n", mazo->cantidad);
        exit(EXIT_FAILURE);
    }
}

void barajar_mazo(mazo_t *mazo)
{
    srand(time(NULL));
    for (int i = 0; i < mazo->cantidad; i++)
    {
        int j = rand() % mazo->cantidad;
        ficha_t temp = mazo->fichas[i];
        mazo->fichas[i] = mazo->fichas[j];
        mazo->fichas[j] = temp;
    }
}

// ----------------------------------------------------------------------
// Funciones para la Mano
// ----------------------------------------------------------------------
void agregar_ficha(mano_t *mano, ficha_t ficha)
{
    if (mano->cantidad < mano->capacidad)
    {
        mano->fichas[mano->cantidad] = ficha;
        mano->cantidad++;
    }
    else
    {
        // Si la mano está llena, aumentar la capacidad y reasignar memoria
        mano->capacidad *= 2;
        mano->fichas = realloc(mano->fichas, sizeof(ficha_t) * mano->capacidad);
        if (mano->fichas == NULL)
        {
            fprintf(stderr, "Error al reasignar memoria para la mano\n");
            exit(EXIT_FAILURE);
        }
        mano->fichas[mano->cantidad] = ficha;
        mano->cantidad++;
    }
}

void mostrar_mano(mano_t *mano)
{
    printf("\nFichas en mano (%d):\n", mano->cantidad);
    printf("────────────────────────\n");
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (mano->fichas[i].numero == 0)
        {
            printf("[%2d] Comodín\n", i + 1);
        }
        else
        {
            printf("[%2d] %2d de %s\n", 
                   i + 1, 
                   mano->fichas[i].numero, 
                   mano->fichas[i].color);
        }
    }
    printf("────────────────────────\n");
}

void finalizar_turno(jugador_t *jugador)
{
    jugador->ficha_agregada = false; // Reinicia la bandera para el próximo turno
}

// ----------------------------------------------------------------------
// Funciones de Inicialización de Jugadores y PCB
// ----------------------------------------------------------------------
void inicializar_jugador(jugador_t *jugador, int id, char nombre[], ficha_t fichas[], int cantidad)
{
    jugador->id = id;
    strcpy(jugador->nombre, nombre);
    jugador->mano.cantidad = 0;
    // Suponemos que "cantidad" es el número de fichas iniciales
    for (int i = 0; i < cantidad; i++)
    {
        agregar_ficha(&(jugador->mano), fichas[i]);
    }
}

void escribir_pcb(pcb_t jugador) {
    char filename[30];
    sprintf(filename, "PCB_Jugador%d.txt", jugador.id_jugador);

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        printf("Error al abrir %s\n", filename);
        return;
    }

    // Convertir estado enum a texto
    const char *estado_str = (jugador.estado == LISTO) ? "LISTO" :
                             (jugador.estado == EJECUTANDO) ? "EJECUTANDO" :
                             "DE_ESPERA";

    // Escribir datos al archivo
    fprintf(file, "===== PCB del Jugador %d =====\n", jugador.id_jugador);
    fprintf(file, "Nombre: %s\n", jugador.nombre);
    fprintf(file, "Estado: %s\n", estado_str);
    fprintf(file, "Fichas en Mano: %d\n", jugador.fichas_en_mano);
    fprintf(file, "Puntos: %d\n", jugador.puntos);
    fprintf(file, "Partidas Jugadas: %d\n", jugador.partidas_jugadas);
    fprintf(file, "Partidas Ganadas: %d\n", jugador.partidas_ganadas);
    fprintf(file, "Partidas Perdidas: %d\n", jugador.partidas_perdidas);
    fprintf(file, "Tiempo Total de Juego: %d\n", jugador.tiempo_total_juego);
    fprintf(file, "Turnos Jugados: %d\n", jugador.turnos_jugados);
    fprintf(file, "Fichas Robadas: %d\n", jugador.fichas_robadas);
    fprintf(file, "Fichas Desfichadas: %d\n", jugador.fichas_desfichadas);
    fprintf(file, "Grupos Formados: %d\n", jugador.grupos_formados);
    fprintf(file, "Escaleras Formadas: %d\n", jugador.escaleras_formadas);
    fprintf(file, "Apeadas Realizadas: %d\n", jugador.apeadas_realizadas);
    fprintf(file, "Embones Realizados: %d\n", jugador.embones_realizados);
    fprintf(file, "Victorias con Escalera: %d\n", jugador.victorias_con_escalera);
    fprintf(file, "Tiempo Restante (turno): %d\n", jugador.tiempo_restante);
    fprintf(file, "Tiempo Bloqueado Acumulado: %d\n", jugador.tiempo_de_espera);

    fclose(file);
}


void actualizar_y_escribir_pcb(pcb_t *pcb, jugador_t *jugador)
{
    // Guardamos la cantidad de fichas anterior
    int fichas_anteriores = pcb->fichas_en_mano;

    // Actualizar información básica del jugador
    pcb->id_jugador = jugador->id;
    strcpy(pcb->nombre, jugador->nombre);
    pcb->fichas_en_mano = jugador->mano.cantidad;

    // Actualizar puntos acumulados
    pcb->puntos = calcular_puntos_mano(&jugador->mano);

    // Verificar si robó ficha(s)
    if (jugador->mano.cantidad > fichas_anteriores)
    {
        int robadas = jugador->mano.cantidad - fichas_anteriores;
        pcb->fichas_robadas += robadas;
    }

    // Determinar el estado del jugador
    if (!jugador->en_juego)
    {
        pcb->estado = DE_ESPERA;
    }
    else if (jugador->tiempo_restante > 0)
    {
        pcb->estado = EJECUTANDO;
    }
    else
    {
        pcb->estado = LISTO;
    }

    // Actualizar estadísticas
    pcb->turnos_jugados++;
    pcb->tiempo_restante = jugador->tiempo_restante;

    // Aumentar tiempo total si está ejecutando
    if (pcb->estado == EJECUTANDO)
    {
        pcb->tiempo_total_juego += jugador->tiempo_restante;
    }

    // Si está de_espera, aumentar tiempo de_espera
    if (pcb->estado == DE_ESPERA)
    {
        pcb->tiempo_de_espera++;
    }

    // Escribir PCB actualizado a archivo
    escribir_pcb(*pcb);
}

void actualizar_tabla_procesos(pcb_t jugadores[], int num_jugadores)
{
    FILE *file = fopen("tabla_procesos.txt", "w");
    if (file == NULL)
    {
        printf("Error al abrir tabla_procesos.txt\n");
        return;
    }

    fprintf(file, "ID\tNombre\tFichas\tPuntos\tEstado\tTurnos\tRobadas\tDesfichadas\tGrupos\tEscaleras\tTiempoJ\tTiempoBloq\n");

    for (int i = 0; i < num_jugadores; i++)
    {
        const char *estado_str = (jugadores[i].estado == LISTO) ? "LISTO" : (jugadores[i].estado == EJECUTANDO) ? "EJECUTANDO"
                                                                                                                : "DE_ESPERA";

        fprintf(file, "%d\t%s\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                jugadores[i].id_jugador, jugadores[i].nombre,
                jugadores[i].fichas_en_mano, jugadores[i].puntos,
                estado_str, jugadores[i].turnos_jugados,
                jugadores[i].fichas_robadas, jugadores[i].fichas_desfichadas,
                jugadores[i].grupos_formados, jugadores[i].escaleras_formadas,
                jugadores[i].tiempo_total_juego, jugadores[i].tiempo_de_espera);
    }

    fclose(file);
}

// ----------------------------------------------------------------------
// Función para repartir fichas entre jugadores y mostrar banco de apeadas
// ----------------------------------------------------------------------
void repartir_fichas(jugador_t jugadores[], int num_jugadores, mazo_t *mazo)
{
    int fichas_por_jugador = (mazo->cantidad * 2) / (3 * num_jugadores);
    int index = 0;

    for (int i = 0; i < num_jugadores; i++)
    {
        for (int j = 0; j < fichas_por_jugador; j++)
        {
            agregar_ficha(&(jugadores[i].mano), mazo->fichas[index]);
            index++;
        }
    }
    mazo->cantidad -= index; // El resto queda en la banca
}

// Muestra los grupos y escaleras del banco de apeadas
void mostrar_banco(const banco_de_apeadas_t *banco)
{
    if (banco == NULL)
    {
        printf("\nEl banco de apeadas está vacío.\n");
        return;
    }

    printf("\n=== Banco de Apeadas ===\n");

    // Mostrar grupos
    if (banco->total_grupos > 0)
    {
        printf("\n── Grupos ──\n");
        for (int i = 0; i < banco->total_grupos; i++)
        {
            printf("Grupo %d: ", i + 1);
            for (int j = 0; j < banco->grupos[i].cantidad; j++)
            {
                if (banco->grupos[i].fichas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", banco->grupos[i].fichas[j].numero, banco->grupos[i].fichas[j].color);
                }
            }
            printf("\n");
        }
    }
    else
    {
        printf("\nNo hay grupos en el banco.\n");
    }

    // Mostrar escaleras
    if (banco->total_escaleras > 0)
    {
        printf("\n── Escaleras ──\n");
        for (int i = 0; i < banco->total_escaleras; i++)
        {
            printf("Escalera %d: ", i + 1);
            for (int j = 0; j < banco->escaleras[i].cantidad; j++)
            {
                if (banco->escaleras[i].fichas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", banco->escaleras[i].fichas[j].numero, banco->escaleras[i].fichas[j].color);
                }
            }
            printf("\n");
        }
    }
    else
    {
        printf("\nNo hay escaleras en el banco.\n");
    }

    printf("────────────────────────────\n");
}

// ----------------------------------------------------------------------
// Funciones de Concurrencia y Manejo de Turnos
// ----------------------------------------------------------------------

// Hilo principal que controla el estado global del juego
void *hilo_juego_func(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&mutex);

        // Detectar ganador continuamente
        int ganador = determinar_ganador(jugadores, NUM_JUGADORES, mazo.cantidad == 0);
        if (ganador != -1)
        {
            printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);

            // Actualizar estadísticas de todos los jugadores
            for (int i = 0; i < NUM_JUGADORES; i++)
            {
                pcbs[i].partidas_jugadas++;
                if (i == ganador)
                    pcbs[i].partidas_ganadas++;
                else
                    pcbs[i].partidas_perdidas++;
                escribir_pcb(pcbs[i]);
            }

            exit(0);
        }

        // Cambiar política de planificación con teclas F/R
        if (kbhit())
        {
            char c = getchar();
            if (c == 'F' || c == 'f')
                modo = 'F';
            else if (c == 'R' || c == 'r')
                modo = 'R';
            printf("\nPolítica cambiada a %c\n", modo);
        }

        pthread_mutex_unlock(&mutex);
        sleep(1); // Revisar estado cada segundo
    }
    return NULL;
}

int calcular_quantum_dinamico(int jugadores_listos) {
    int q = 20 / (jugadores_listos + 1);
    return (q < 5) ? 5 : q;
}

void decidir_politica() {
    int total_espera = 0, jugadores_bloqueados = 0;
    int jugadores_listos = num_listos;

    pthread_mutex_lock(&mutex_pcbs);
    for (int i = 0; i < NUM_JUGADORES; i++) {
        if (pcbs[i].estado == DE_ESPERA) {
            total_espera += pcbs[i].tiempo_de_espera;
            jugadores_bloqueados++;
        }
    }
    pthread_mutex_unlock(&mutex_pcbs);

    if (jugadores_bloqueados > 1 || jugadores_listos >= NUM_JUGADORES / 2) {
        // Modo Round Robin
        modo = 'R';
        QUANTUM = 20 / (jugadores_listos + 1);
        if (QUANTUM < 5) QUANTUM = 5;
        printf("\n[Cambio] Modo RR - Quantum: %d segundos\n", QUANTUM);
    } else {
        // Modo FCFS con quantum dinámico
        modo = 'F';
        
        // Calcular quantum basado en tiempo de espera promedio y jugadores activos
        int promedio_espera = (jugadores_bloqueados > 0) ? 
                             (total_espera / jugadores_bloqueados) : 0;
                             
        QUANTUM = 15 + (promedio_espera * 2) - (jugadores_listos * 1);
        
        // Aplicar límites
        if (QUANTUM < 10) QUANTUM = 10;       // Mínimo 10s
        else if (QUANTUM > 30) QUANTUM = 30;  // Máximo 30s
        
        printf("\n[Cambio] Modo FCFS - Quantum ajustado: %d segundos\n", QUANTUM);
    }
}



// Hilo planificador gestiona turnos de los jugadores
void *planificador(void *arg)
{
    hilo_control_t *control = (hilo_control_t *)arg;
    struct timespec sleep_time = {0, 100000000}; // 100ms

    while (!*(control->terminar_flag))
    {
        decidir_politica(); // Actualiza la política en cada ciclo
        pthread_mutex_lock(&mutex);

        if (proceso_en_ejecucion == -1)
        {
            int siguiente = siguiente_turno();
            if (siguiente != -1)
            {
                proceso_en_ejecucion = siguiente;
                jugador_t *jugador = &jugadores[siguiente - 1];
                
                // Actualizar PCB al pasar a EJECUTANDO
                pcbs[siguiente - 1].estado = EJECUTANDO;
                escribir_pcb(pcbs[siguiente - 1]);
                
                printf("\n[PLANIFICADOR] Jugador %d (%s) pasa a EJECUTANDO\n", 
                       siguiente, jugador->nombre);

                pthread_t hilo_jugador;
                pthread_create(&hilo_jugador, NULL, jugador_thread, jugador);
                pthread_detach(hilo_jugador);

                // Esperar a que termine el turno o se agote el quantum
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += QUANTUM;
                pthread_cond_timedwait(control->cond, &mutex, &timeout);

                proceso_en_ejecucion = -1;

                // El jugador siempre pasa a DE_ESPERA, independientemente de la política
                int tiempo_bloqueo = rand() % 30 + 5; // Entre 1-3 segundos
                printf("\n[PLANIFICADOR] Jugador %d (%s) pasa a DE_ESPERA por %d segundos\n", 
                       siguiente, jugador->nombre, tiempo_bloqueo);
                
                // La función bloquear_jugador se encarga de actualizar el PCB y la tabla
                bloquear_jugador(siguiente, tiempo_bloqueo);
                pcbs[siguiente-1].tiempo_total_juego += QUANTUM; //Registra tiempo de juego usado

                verificar_cola_de_esperas(); 
                // Nota: No agregamos inmediatamente a la cola de listos
                // El jugador SIEMPRE debe pasar por DE_ESPERA primero
                // La función verificar_cola_de_esperas se encargará de moverlo 
                // a la cola de listos una vez que haya cumplido su tiempo de espera
            }
        }

        pthread_mutex_unlock(&mutex);
        nanosleep(&sleep_time, NULL);
    }
    return NULL;
}

// Función para bloquear jugador
void bloquear_jugador(int id_jugador, int tiempo) {
    pthread_mutex_lock(&mutex_colas);
    
    // Paso 1: Remover de cola_listos si está presente
    bool encontrado = false;
    for (int i = 0; i < num_listos; i++) {
        if (cola_listos[i] == id_jugador) {
            // Eliminar desplazando elementos
            for (int j = i; j < num_listos - 1; j++) {
                cola_listos[j] = cola_listos[j + 1];
            }
            num_listos--;
            encontrado = true;
            printf("[DEBUG] Jugador %d removido de LISTOS\n", id_jugador);
            break;
        }
    }
    
    // Paso 2: Verificar si ya está en espera
    bool ya_en_espera = false;
    for (int i = 0; i < num_de_esperas; i++) {
        if (cola_de_esperas[i] == id_jugador) {
            ya_en_espera = true;
            pcbs[id_jugador-1].tiempo_de_espera = tiempo;
            printf("[DEBUG] Jugador %d actualizado en DE_ESPERA: %ds\n", 
                  id_jugador, tiempo);
            break;
        }
    }
    
    // Paso 3: Agregar a espera si no estaba
    if (!ya_en_espera && num_de_esperas < NUM_JUGADORES) {
        cola_de_esperas[num_de_esperas++] = id_jugador;
        pcbs[id_jugador-1].estado = DE_ESPERA;
        pcbs[id_jugador-1].tiempo_de_espera = tiempo;
        printf("[DEBUG] Jugador %d -> DE_ESPERA (%ds)\n", 
              id_jugador, tiempo);
    }
    
    // Actualizaciones comunes
    escribir_pcb(pcbs[id_jugador-1]);
    actualizar_tabla_procesos(pcbs, NUM_JUGADORES);
    
    pthread_mutex_unlock(&mutex_colas);
}

// ----------------------------------------------------------------------
// Función del hilo del jugador
// ----------------------------------------------------------------------

// Hilo que representa a cada jugador en el juego
// Cada jugador tiene su propio hilo que ejecuta esta función
// El jugador espera su turno, realiza acciones y luego pasa el turno

void *jugador_thread(void *arg)
{
    jugador_t *jugador = (jugador_t *)arg;
    pcb_t *mi_pcb = &pcbs[jugador->id - 1];
    struct timespec start, now;
    bool turno_activo = true;

    clock_gettime(CLOCK_MONOTONIC, &start);

    while (turno_activo && !juego_terminado)
    {
        pthread_mutex_lock(&mutex);

        printf("\n=== Turno de %s ===\n", jugador->nombre);
        mostrar_mano(&jugador->mano);
        printf("\nOpciones:\n");
        printf("1. Robar ficha\n2. Hacer apeada\n3. Embonar ficha\n");
        printf("4. Mostrar banco\n5. Pasar turno\n");
        printf("\nSeleccione (1-5): ");
        fflush(stdout);

        pthread_mutex_unlock(&mutex);

        int opcion = -1;
        char input[10] = {0};
        bool hizo_accion = false;

        while (true)
        {
            struct pollfd mypoll = {STDIN_FILENO, POLLIN, 0};
            int ret = poll(&mypoll, 1, 200);

            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start.tv_sec);

            if (elapsed >= QUANTUM)
            {
                printf("\n¡Tiempo agotado para %s!\n", jugador->nombre);
                turno_activo = false;
                break;
            }

            if (ret > 0 && read(STDIN_FILENO, input, sizeof(input)) > 0)
            {
                opcion = atoi(input);
                break;
            }
        }

        pthread_mutex_lock(&mutex);

        switch (opcion)
        {
        case 1:
            if (mazo.cantidad > 0)
            {
                ficha_t nueva = mazo.fichas[--mazo.cantidad];
                agregar_ficha(&jugador->mano, nueva);
                mostrar_robo_ficha(&nueva, false);
                jugador->ficha_agregada = true;
                turno_activo = false;
                hizo_accion = true;
                
                // Actualizar PCB después de robar
                mi_pcb->fichas_en_mano = jugador->mano.cantidad;
                mi_pcb->fichas_robadas++;
            }
            break;

        case 2:
            if (puede_hacer_apeada(jugador))
            {
                apeada_t apeada = calcular_mejor_apeada_aux(jugador);
                if (realizar_apeada_optima(jugador, &banco_apeadas))
                {
                    printf("\n¡Apeada exitosa!\n");
                    mostrar_apeada(&apeada);
                    hizo_accion = true;
                    
                    // Actualizar PCB después de apear
                    mi_pcb->fichas_en_mano = jugador->mano.cantidad;
                    mi_pcb->apeadas_realizadas++;
                    
                    if (modo == 'F')
                    {
                        turno_activo = false;
                    }
                }
                apeada_liberar(&apeada);
            }
            else
            {
                printf("\nNo tienes combinaciones válidas para apear\n");
            }
            break;

        case 3:
            if (jugador->mano.cantidad > 0)
            {
                mostrar_mano(&jugador->mano);
                printf("\nSeleccione ficha (1-%d): ", jugador->mano.cantidad);
                fflush(stdout);

                struct pollfd ficha_poll = {STDIN_FILENO, POLLIN, 0};
                char input2[10] = {0};
                while (true)
                {
                    int ret = poll(&ficha_poll, 1, 200);
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    double elapsed = (now.tv_sec - start.tv_sec);

                    if (modo == 'R' && elapsed >= QUANTUM)
                    {
                        printf("\n¡Tiempo agotado para %s!\n", jugador->nombre);
                        turno_activo = false;
                        break;
                    }

                    if (ret > 0 && read(STDIN_FILENO, input2, sizeof(input2)) > 0)
                    {
                        int idx = atoi(input2) - 1;
                        if (idx >= 0 && idx < jugador->mano.cantidad)
                        {
                            if (embonar_ficha(jugador, &banco_apeadas, idx))
                            {
                                printf("\n¡Ficha embonada con éxito!\n");
                                hizo_accion = true;
                                
                                // Actualizar PCB después de embonar
                                mi_pcb->fichas_en_mano = jugador->mano.cantidad;
                                mi_pcb->embones_realizados++;
                                
                                if (modo == 'F')
                                {
                                    turno_activo = false;
                                }
                            }
                            else
                            {
                                printf("\nNo se pudo embonar la ficha\n");
                            }
                        }
                        else
                        {
                            printf("\nÍndice inválido\n");
                        }
                        break;
                    }
                }
            }
            else
            {
                printf("\nNo tienes fichas para embonar\n");
            }
            break;

        case 4:
            mostrar_banco(&banco_apeadas);
            break;

        case 5:
            if (mazo.cantidad > 0)
            {
                ficha_t nueva = mazo.fichas[--mazo.cantidad];
                agregar_ficha(&jugador->mano, nueva);
                mostrar_robo_ficha(&nueva, false);
                
                // Actualizar PCB al pasar turno
                mi_pcb->fichas_en_mano = jugador->mano.cantidad;
                mi_pcb->fichas_robadas++;
                printf("\nHas pasado el turno y robado una ficha.\n");
            }
            else
            {
                printf("\nNo se puede robar una ficha: el mazo está vacío.\n");
            }
        
            turno_activo = false;
            hizo_accion = true;
            break;

        default:
            if (opcion != -1)
            {
                printf("\nOpción inválida. Por favor seleccione 1-5\n");
            }
            break;
        }

        pthread_mutex_unlock(&mutex);

        // Si hubo acción, actualizar PCB y tabla
        if (hizo_accion)
        {
            actualizar_y_escribir_pcb(mi_pcb, jugador);
            actualizar_tabla_procesos(pcbs, NUM_JUGADORES);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec);
        if (elapsed >= QUANTUM)
        {
            printf("\n¡Quantum completado para %s!\n", jugador->nombre);
            turno_activo = false;
        }

        jugador->tiempo_restante = QUANTUM - (int)elapsed;

        if (!turno_activo)
        {
            printf("\n=== Fin de turno de %s ===\n", jugador->nombre);
            
            // No es necesario actualizar el estado aquí, el planificador lo hará
            // al finalizar el turno del jugador
        }

        // Verificar condiciones de victoria
        if (jugador->mano.cantidad == 0)
        {
            printf("\n¡%s se ha quedado sin fichas y gana el juego!\n", jugador->nombre);
            juego_terminado = true;
            return NULL;
        }

        if (mazo.cantidad == 0)
        {
            printf("\n¡El mazo se ha agotado!\n");
            int ganador = determinar_ganador(jugadores, NUM_JUGADORES, true);
            printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);
            juego_terminado = true;
            return NULL;
        }
    }

    return NULL;
}

// Función para agregar a cola de listos
void agregar_a_cola_listos(int id_jugador) {
    pthread_mutex_lock(&mutex_colas);
    
    // Verificar capacidad máxima de la cola
    if ((final + 1) % NUM_JUGADORES == frente) {
        printf("[ERROR] Cola de listos llena (Jugador %d)\n", id_jugador);
        pthread_mutex_unlock(&mutex_colas);
        return;
    }
    
    // Verificar si ya está en cola
    for (int i = frente; i != final; i = (i+1) % NUM_JUGADORES) {
        if (cola_listos[i] == id_jugador) {
            printf("[WARN] Jugador %d ya en LISTOS\n", id_jugador);
            pthread_mutex_unlock(&mutex_colas);
            return;
        }
    }
    
    // Agregar a cola
    cola_listos[final] = id_jugador;
    final = (final + 1) % NUM_JUGADORES;
    
    // Actualizar PCB
    pcbs[id_jugador-1].estado = LISTO;
    escribir_pcb(pcbs[id_jugador-1]);
    
    printf("[DEBUG] Jugador %d agregado a LISTOS\n", id_jugador);
    
    pthread_mutex_unlock(&mutex_colas);
}


// Función para inicializar PCBs
void inicializar_pcbs()
{
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        // Identificación básica
        pcbs[i].id_jugador = i + 1;
        snprintf(pcbs[i].nombre, sizeof(pcbs[i].nombre), "Jugador %d", i + 1);

        // Estado del proceso/jugador
        pcbs[i].estado = LISTO;       // Estado inicial (LISTO, EJECUTANDO, DE_ESPERA)
        pcbs[i].tiempo_de_espera = 0; // Tiempo restante de bloqueo

        // Estadísticas del juego
        pcbs[i].fichas_en_mano = FICHAS_INICIALES;
        pcbs[i].puntos = 0;
        pcbs[i].partidas_jugadas = 0;
        pcbs[i].partidas_ganadas = 0;
        pcbs[i].partidas_perdidas = 0;

        // Tiempos y contadores
        pcbs[i].tiempo_total_juego = 0;
        pcbs[i].turnos_jugados = 0;
        pcbs[i].tiempo_restante = QUANTUM;

        // Acciones del juego
        pcbs[i].fichas_robadas = 0;
        pcbs[i].fichas_desfichadas = 0;
        pcbs[i].grupos_formados = 0;
        pcbs[i].escaleras_formadas = 0;
        pcbs[i].apeadas_realizadas = 0;
        pcbs[i].embones_realizados = 0;
        pcbs[i].victorias_con_escalera = 0;

        // Agregar a la cola de listos inicial
        agregar_a_cola_listos(pcbs[i].id_jugador);
    }
}

// Hilo que verifica jugadores de_esperas periódicamente
void *verificar_de_esperas(void *arg)
{
    hilo_control_t *control = (hilo_control_t *)arg;
    struct timespec sleep_time = {0, 100000000}; // 100ms

    while (1)
    {
        pthread_mutex_lock(control->mutex);
        if (*(control->terminar_flag))
        {
            pthread_mutex_unlock(control->mutex);
            break;
        }
        pthread_mutex_unlock(control->mutex);

        pthread_mutex_lock(&mutex);
        verificar_cola_de_esperas();
        pthread_mutex_unlock(&mutex);

        // Usar nanosleep en lugar de usleep
        nanosleep(&sleep_time, NULL);
    }

    return NULL;
}

// Función para obtener el siguiente jugador en la cola de listos
int siguiente_turno()
{
    int id = -1;
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;

    // Usar trylock para evitar bloqueos
    if (pthread_mutex_trylock(&mutex) == 0)
    {
        if (num_listos > 0)
        {
            id = cola_listos[frente];
            frente = (frente + 1) % NUM_JUGADORES;
            num_listos--;
        }
        pthread_mutex_unlock(&mutex);
    }

    return id;
}

void reiniciar_cola_listos()
{
    pthread_mutex_lock(&mutex);

    frente = 0;
    final = 0;
    num_listos = 0;

    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        if (jugadores[i].en_juego)
        {
            cola_listos[final] = jugadores[i].id;
            final = (final + 1) % NUM_JUGADORES;
            num_listos++;
            pcbs[i].estado = LISTO; // Añadir esta línea
        }
    }

    pthread_mutex_unlock(&mutex);
    pthread_cond_signal(&cond_turno); // Notificar al planificador
}

void mover_a_cola_de_esperas(int id_jugador)
{
    // Bloquear el jugador en su PCB
    pcbs[id_jugador - 1].estado = DE_ESPERA;
    pcbs[id_jugador - 1].tiempo_de_espera = rand() % 10 + 1;

    // Agregar a la cola de de_esperas
    if (num_de_esperas < NUM_JUGADORES)
    {
        cola_de_esperas[num_de_esperas++] = id_jugador;
    }

    printf("\nJugador %d de_espera por %d segundos\n", id_jugador, pcbs[id_jugador - 1].tiempo_de_espera);
}

void verificar_cola_de_esperas() {
    pthread_mutex_lock(&mutex_colas);
    
    for (int i = 0; i < num_de_esperas; i++) {
        int id = cola_de_esperas[i];
        
        // Reducir tiempo y verificar
        if (--pcbs[id-1].tiempo_de_espera <= 0) {
            // Paso 1: Mover a listos (con verificación)
            bool agregado = false;
            for (int j = 0; j < num_listos; j++) {
                if (cola_listos[j] == id) {
                    agregado = true;
                    break;
                }
            }
            
            if (!agregado && num_listos < NUM_JUGADORES) {
                cola_listos[num_listos++] = id;
                pcbs[id-1].estado = LISTO;
                printf("[DEBUG] Jugador %d -> LISTO\n", id);
            }
            
            // Paso 2: Eliminar de esperas
            for (int j = i; j < num_de_esperas-1; j++) {
                cola_de_esperas[j] = cola_de_esperas[j+1];
            }
            num_de_esperas--;
            i--;  // Ajustar índice
            
            // Paso 3: Actualizar registros
            escribir_pcb(pcbs[id-1]);
        }
    }
    
    pthread_mutex_unlock(&mutex_colas);
    actualizar_tabla_procesos(pcbs, NUM_JUGADORES);
}


void iniciar_concurrencia()
{
    pthread_t hilos[NUM_JUGADORES];

    // Crear un hilo para cada jugador
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        pthread_create(&hilos[i], NULL, jugador_thread, (void *)&jugadores[i]);
    }

    // Esperar a que todos los hilos terminen
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        pthread_join(hilos[i], NULL);
    }
}

// ----------------------------------------------------------------------
// Funciones para Algoritmos de Planificación (FCFS y Round Robin)
// ----------------------------------------------------------------------

// Función para capturar teclas sin bloqueo
int kbhit()
{
    struct termios oldt, newt;
    int ch, oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

// Muestra el estado actual del juego
void mostrar_estado_juego()
{
    pthread_mutex_lock(&mutex);

    printf("\n=== ESTADO ACTUAL ===\n");
    mostrar_banco(&banco_apeadas);

    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        printf("\nJugador %d (%s): %d fichas, %d puntos\n",
               jugadores[i].id,
               jugadores[i].nombre,
               jugadores[i].mano.cantidad,
               calcular_puntos_mano(&jugadores[i].mano));
    }

    pthread_mutex_unlock(&mutex);
}

// Manejador de turnos (se usa en un hilo)
void *manejar_turnos(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&mutex);

        // Si no hay jugadores listos, esperar
        if (frente == final)
        {
            pthread_mutex_unlock(&mutex);
            sleep(1);
            continue;
        }

        int jugador_id = siguiente_turno(); // Obtener siguiente jugador

        // Seleccionar el jugador según el modo de planificación
        if (modo == 'F') // FCFS
        {
            jugador_id = cola_listos[frente];      // Tomar el jugador al frente de la cola
            frente = (frente + 1) % NUM_JUGADORES; // Avanzar el frente
        }
        else if (modo == 'R') // Round Robin
        {
            jugador_id = cola_listos[frente];      // Tomar el jugador al frente de la cola
            frente = (frente + 1) % NUM_JUGADORES; // Avanzar el frente

            // Reinsertar al final de la cola si sigue en juego
            if (jugadores[jugador_id - 1].en_juego)
            {
                cola_listos[final] = jugador_id;
                final = (final + 1) % NUM_JUGADORES;
            }
        }

        turno_actual = jugador_id;
        jugador_t *jugador_actual = &jugadores[jugador_id - 1];

        printf("\nTurno del jugador: %s (ID: %d)\n", jugador_actual->nombre, jugador_id);

        // Verificar si el jugador tiene tiempo restante
        if (jugador_actual->tiempo_restante <= 0)
        {
            printf("\nJugador %s ha perdido su turno por falta de tiempo.\n", jugador_actual->nombre);
            pthread_mutex_unlock(&mutex);
            continue; // Pasar al siguiente jugador
        }

        // Simulación del turno
        int tiempo_juego = (modo == 'R' && jugador_actual->tiempo_restante > QUANTUM) ? QUANTUM : jugador_actual->tiempo_restante;
        sleep(tiempo_juego);
        jugador_actual->tiempo_restante -= tiempo_juego;

        if (jugador_actual->tiempo_restante <= 0)
        {
            printf("\nTiempo agotado para el jugador %s\n", jugador_actual->nombre);
        }

        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
    return NULL;
}

void mostrar_politica_actual() {
    const char* politica = (modo == 'F') ? 
        "FCFS (Turnos completos en orden de llegada)" : 
        "Round Robin (Quantum de %d segundos)";
    
    if (modo == 'F') {
        printf("\nPolítica actual: %s\n", politica);
    } else {
        printf("\nPolítica actual: ");
        printf(politica, QUANTUM);
        printf("\n");
    }
    printf("───────────────────────────────────────────────────────\n");
}

void elegir_politica()
{
    printf("\n╔════════════════════════════════════════════╗");
    printf("\n║   SELECCIÓN DE POLÍTICA DE PLANIFICACIÓN   ║");
    printf("\n╠════════════════════════════════════════════╣");
    printf("\n║ F - First Come First Served (FCFS)         ║");
    printf("\n║ R - Round Robin (Quantum %d segundos)      ║", QUANTUM);
    printf("\n╚════════════════════════════════════════════╝");
    printf("\nSeleccione política (F/R): ");

    char opcion;
    scanf(" %c", &opcion);
    getchar(); // Limpiar buffer

    if (opcion == 'F' || opcion == 'f')
    {
        modo = 'F';
        printf("\nPolítica cambiada a FCFS: Turnos completos en orden de llegada\n");
    }
    else if (opcion == 'R' || opcion == 'r')
    {
        modo = 'R';
        printf("\nPolítica cambiada a Round Robin: Quantum de %d segundos\n", QUANTUM);
    }
    else
    {
        printf("\nManteniendo política actual: %s\n",
               (modo == 'F') ? "FCFS" : "Round Robin");
    }

    mostrar_politica_actual();
}

// Función auxiliar para mostrar mensajes de robo de ficha
void mostrar_robo_ficha(const ficha_t *ficha, bool es_automatico)
{
    printf("%s: %d de %s\n",
           es_automatico ? "\nRobaste automáticamente" : "Robaste",
           ficha->numero,
           ficha->color);
}

// ----------------------------------------------------------------------
// Función Principal (Versión Mejorada)
// ----------------------------------------------------------------------
int main()
{
    // 1. Inicialización
    srand(time(NULL));
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&mutex_mesa, NULL);
    pthread_mutex_init(&mutex_terminacion, NULL);
    pthread_cond_init(&cond_turno, NULL);

    // 2. Estructuras de control
    volatile bool juego_terminado = false;
    hilo_control_t control = {
        .terminar_flag = &juego_terminado,
        .mutex = &mutex_terminacion,
        .cond = &cond_turno};

    // 3. Inicializar juego
    inicializar_mazo(&mazo);
    barajar_mazo(&mazo);
    banco_inicializar(&banco_apeadas);
    inicializar_jugadores(&mazo);
    inicializar_pcbs();

    // 4. Configurar nombres de jugadores
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        char nombre[MAX_NOMBRE];
        printf("\nIngrese nombre para Jugador %d: ", i + 1);

        if (fgets(nombre, MAX_NOMBRE, stdin) == NULL)
        {
            perror("Error leyendo nombre");
            exit(EXIT_FAILURE);
        }

        nombre[strcspn(nombre, "\n")] = '\0';
        strncpy(jugadores[i].nombre, nombre, MAX_NOMBRE - 1);
        jugadores[i].nombre[MAX_NOMBRE - 1] = '\0';

        strncpy(pcbs[i].nombre, nombre, MAX_NOMBRE - 1);
        pcbs[i].nombre[MAX_NOMBRE - 1] = '\0';

        escribir_pcb(pcbs[i]);
    }

    mostrar_politica_actual();
    // Elegir política de planificación
    elegir_politica();
    printf("\n=== JUEGO INICIADO CON POLÍTICA %s ===\n",
           (modo == 'F') ? "FCFS" : "Round Robin");

    // 5. Crear hilos
    pthread_t hilo_es, hilo_planificador;
    if (pthread_create(&hilo_es, NULL, verificar_de_esperas, &control) != 0 ||
        pthread_create(&hilo_planificador, NULL, planificador, &control) != 0)
    {
        perror("Error creando hilos");
        exit(EXIT_FAILURE);
    }

    // 6. Bucle principal del juego
    int ronda = 1;
    bool juego_activo = true;

    while (juego_activo)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // Timeout de 1 segundo

        // Verificar ganador
        if (pthread_mutex_timedlock(&mutex, &ts) == 0)
        {
            int ganador = determinar_ganador(jugadores, NUM_JUGADORES, mazo.cantidad == 0);
            if (ganador != -1)
            {
                printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);
                juego_activo = false;
            }
            pthread_mutex_unlock(&mutex);
        }

        // Manejo de turnos
        int jugador_actual = siguiente_turno();
        if (jugador_actual != -1)
        {
            jugador_thread(&jugadores[jugador_actual - 1]);

            // Control de rondas
            static int turnos_en_ronda = 0;
            if (++turnos_en_ronda >= NUM_JUGADORES)
            {
                printf("\n=== Fin de ronda %d ===\n", ronda++);
                turnos_en_ronda = 0;
                elegir_politica();
                mostrar_estado_juego();
            }
        }
        else
        {
            reiniciar_cola_listos();
            nanosleep((const struct timespec[]){{0, 100000000}}, NULL); // 100ms
        }
    }

    // 7. Finalización
    pthread_mutex_lock(&mutex_terminacion);
    juego_terminado = true;
    pthread_mutex_unlock(&mutex_terminacion);

    pthread_cond_broadcast(&cond_turno);
    pthread_join(hilo_es, NULL);
    pthread_join(hilo_planificador, NULL);

    // 8. Liberar recursos
    banco_liberar(&banco_apeadas);
    liberar_jugadores();
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex_mesa);
    pthread_mutex_destroy(&mutex_terminacion);
    pthread_cond_destroy(&cond_turno);

    return 0;
}