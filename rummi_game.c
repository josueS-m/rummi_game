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
#include <fcntl.h>

// ----------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------
#define NUM_JUGADORES 4          // Número fijo de jugadores
#define MAX_CARTAS 108           // Total cartas en el mazo (standard para Rummy)
#define QUANTUM 20               // Tiempo por turno en segundos
#define PUNTOS_MINIMOS_APEADA 30 // Mínimo para primera apeada

#define CARTAS_INICIALES 14 // Cartas al repartir (puede variar según reglas)
#define MAX_COLOR 15        // Longitud máxima para nombre de color
#define MAX_NOMBRE 50       // Longitud máxima para nombre jugador

#define MAX_CARTAS_GRUPO 4     // Máximo cartas en grupo (ej: 4 sietes)
#define MAX_CARTAS_ESCALERA 13 // Máximo en escalera (A-2-...-K)
#define MAX_GRUPOS 10          // Máximo grupos en mesa
#define MAX_ESCALERAS 10       // Máximo escaleras en mesa
#define VALOR_COMODIN 0        // Valor numérico para comodines
#define MIN_CARTAS_GRUPO 3     // Mínimo cartas para un grupo
#define MIN_CARTAS_ESCALERA 3  // Mínimo cartas para escalera

#define TURNO_MAXIMO 30 // 30 segundos por turno

#define TAMANO_MEMORIA 102400  
#define TAMANO_BLOQUE 64       // Bloque de 64 bytes
#define TAMANO_MAPA_BITS (TAMANO_MEMORIA / TAMANO_BLOQUE / 8) + 1  // Tamaño del arreglo de bits

#define TAMANO_MEMORIA_LISTA 10240  // 10240 unidades de memoria

#define NUM_PAGINAS 6   // Total de páginas en memoria principal
#define TAMANO_MARCO 4  // Cada marco puede contener 4 páginas

// ----------------------------------------------------------------------
// Estructuras y Tipos
// ----------------------------------------------------------------------

typedef struct
{
    int numero;            // Valor numérico de la carta (1-13 para valores normales)
    char color[MAX_COLOR]; // Color de la carta (ej: "rojo", "azul", "comodín")
} carta_t;

typedef struct
{
    carta_t *cartas; // Array dinámico de cartas
    int cantidad;    // Cartas actuales en mano
    int capacidad;   // Capacidad máxima actual del array
} mano_t;

typedef struct
{
    carta_t cartas[MAX_CARTAS_GRUPO]; // Array estático para grupo (ej: 4 cartas igual número)
    int cantidad;                     // Cartas actuales en el grupo
} grupo_t;

typedef struct
{
    carta_t cartas[MAX_CARTAS_ESCALERA]; // Array estático para escalera (ej: 3+ cartas consecutivas mismo color)
    int cantidad;                        // Cartas actuales en la escalera
} escalera_t;

typedef struct
{
    grupo_t *grupos;       // Array dinámico de grupos en mesa
    escalera_t *escaleras; // Array dinámico de escaleras en mesa
    int total_grupos;      // Grupos actuales
    int total_escaleras;   // Escaleras actuales
} banco_de_apeadas_t;

typedef enum
{
    BLOQUEADO,
    LISTO,
    EJECUTANDO
} estado_jugador;

typedef struct {
    int id;               // ID único (1-4)
    char nombre[MAX_NOMBRE];      // Nombre del jugador
    estado_jugador estado;        // Estado (BLOQUEADO/LISTO/EJECUTANDO)
    int tiempo_restante;          // Tiempo restante en turno (para Round Robin)
    int tiempo_bloqueado;         // Tiempo acumulado en E/S
    int memoria_asignada;         // Para Parte II (gestión de memoria)
    int puntos;                   // Puntos acumulados en partida
    int puntos_mano_actual;       // Puntos en mano actual (para validar 30+)
    int turnos_jugados;           // Turnos totales
    int cartas_robadas;           // Cartas robadas de la banca
    int cartas_embonadas;         // Cartas añadidas a apeadas existentes
    int grupos_formados;          // Ternas/cuaternas creadas
    int puntos_suficientes;  // Flag si alcanzó puntos mínimos
    int escaleras_formadas;       // Escaleras creadas  
    bool carta_agregada;     // Nueva bandera: indica si ya agregó una carta en el turno
    bool en_juego;           // Estado activo/inactivo
    int tiempo_total_juego;       // Tiempo total jugado
    int tiempo_en_turno;          // Tiempo acumulado en turnos activos
    mano_t mano;                  // Cartas en mano (array dinámico)
} jugador_t;

typedef struct
{
    carta_t cartas[MAX_CARTAS]; // Array estático con todas las cartas
    int cantidad;               // Cartas actuales en el mazo
} mazo_t;

typedef struct
{
    grupo_t *grupos;       // Array dinámico de grupos en mesa
    escalera_t *escaleras; // Array dinámico de escaleras en mesa
    int total_grupos;      // Grupos actuales
    int total_escaleras;   // Escaleras actuales
} apeada_t;

typedef struct
{
    int id_jugador;          // ID único del jugador
    char nombre[MAX_NOMBRE]; // Nombre del jugador
    estado_jugador estado;   // Estado actual (LISTO, EJECUTANDO, BLOQUEADO)
    int cartas_en_mano;      // Número de cartas en mano
    int puntos;              // Puntos acumulados
    int tiempo_restante;     // Tiempo restante en el turno actual
    int turnos_jugados;      // Número de turnos jugados
    int cartas_robadas;      // Número de cartas robadas
    int cartas_embonadas;            // Cartas añadidas a apeadas existentes
    int grupos_formados;     // Número de grupos formados
    int escaleras_formadas;  // Número de escaleras formadas
    int tiempo_total_juego;  // Tiempo total desde que el jugador entró al juego hasta el momento actual
    int tiempo_bloqueado;    // Tiempo total en estado bloqueado
    int tiempo_en_turno; // Tiempo total que el jugador ha pasado en sus turnos
    int memoria_asignada;  // Memoria asignada (para Parte II)
} pcb_t;

typedef struct {
    pcb_t pcbs[NUM_JUGADORES];  // Array de PCBs (uno por jugador)
    int jugadores_activos;       // Cuántos jugadores están en juego
    pthread_mutex_t mutex;       // Para acceso thread-safe (evitar race conditions)
} tabla_procesos_t;
    
typedef struct
{
    volatile bool *terminar_flag;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
} hilo_control_t;

//ESTRUCTURAS PARA EL MAPA DE BITS

typedef struct {
    unsigned char mapa_bits[TAMANO_MAPA_BITS];  // Arreglo para el mapa de bits
    int bloques_totales;                   // Número total de bloques
    int bloques_libres;                    // Bloques libres
} ManejadorMemoriaMapaBits;

//ESTRUCTURAS PARA LISTAS LIGADAS

typedef struct bloque_memoria {
    int inicio;
    int tamano;
    int disponible; // 1 si está libre, 0 si está ocupado
    struct bloque_memoria *siguiente;
} BloqueMemoria;

typedef struct {
    BloqueMemoria *lista_bloques;
    int memoria_total;
} GestionMemoria;


//ESTRUCTURAS PARA LA MEMORIA VIRTUAL
typedef struct {
    int id_pagina;  // ID de la página
    bool en_memoria; // Indica si la página está en memoria principal
    bool bit_segunda_oportunidad; // Bit para el algoritmo de segunda oportunidad
} pagina_t;

typedef struct {
    pagina_t paginas[NUM_PAGINAS]; // Tabla de páginas
    int puntero_reemplazo;         // Puntero para el algoritmo de segunda oportunidad
} memoria_virtual_t;

memoria_virtual_t memoria_virtual;

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
int cola_bloqueados[NUM_JUGADORES];                   // Cola de jugadores bloqueados
int num_bloqueados = 0;                               // Contador de jugadores bloqueados
pthread_t hilo_juego;                                 // Hilo del juego
pthread_cond_t cond_turno = PTHREAD_COND_INITIALIZER; // Variable de condición para el juego
int proceso_en_ejecucion = -1;                        // ID del proceso en ejecución
int tiempo_restante_quantum = 0;                      // Tiempo restante del proceso en ejecución

ManejadorMemoriaMapaBits manejador;
GestionMemoria gm;

mazo_t mazo;        // Mazo de cartas
int num_listos = 0; // Número de jugadores en la cola de listos

// ----------------------------------------------------------------------
// Funciones de inicializacion y liberacion
// ----------------------------------------------------------------------

// Prototipos de funciones
void mano_inicializar(mano_t *mano, int capacidad);
int kbhit();
void agregar_a_cola_listos(int id_jugador);
void agregar_carta(mano_t *mano, carta_t carta); // Add declaration for agregar_carta
void acceder_pagina(int id_pagina); // Add declaration for acceder_pagina
int siguiente_turno();
void reiniciar_cola_listos();
void *jugador_thread(void *arg);
bool puede_hacer_apeada(jugador_t *jugador);
int calcular_puntos_apeada(const apeada_t *apeada);
void verificar_cola_bloqueados();
bool es_grupo_valido(const carta_t cartas[], int cantidad);
bool es_escalera_valida(const carta_t cartas[], int cantidad);
void mostrar_robo_carta(const carta_t *carta, bool es_automatico); // Nueva función

void mano_inicializar(mano_t *mano, int capacidad)
{
    if (mano == NULL || capacidad <= 0)
    {
        fprintf(stderr, "Error: Parámetros inválidos para inicializar mano\n");
        exit(EXIT_FAILURE);
    }

    mano->cartas = (carta_t *)malloc(sizeof(carta_t) * capacidad);
    if (mano->cartas == NULL)
    {
        fprintf(stderr, "Error al asignar memoria para la mano\n");
        exit(EXIT_FAILURE);
    }

    mano->cantidad = 0;
    mano->capacidad = capacidad;
}

void inicializar_jugadores(mazo_t *mazo) {
    if (mazo == NULL) {
        fprintf(stderr, "Error: Puntero a mazo inválido\n");
        exit(EXIT_FAILURE);
    }

    // Verificar suficientes cartas para repartir
    if (mazo->cantidad < CARTAS_INICIALES * NUM_JUGADORES) {
        fprintf(stderr, "Error: No hay suficientes cartas en el mazo (%d necesarias, %d disponibles)\n",
                CARTAS_INICIALES * NUM_JUGADORES, mazo->cantidad);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_JUGADORES; i++) {
        // Inicialización de propiedades básicas
        jugadores[i].id = i + 1;  // IDs comienzan en 1
        snprintf(jugadores[i].nombre, MAX_NOMBRE, "Jugador %d", i + 1);
        jugadores[i].estado = LISTO;
        jugadores[i].en_juego = true;
        jugadores[i].carta_agregada = false;
        jugadores[i].puntos_suficientes = false;

        // Inicialización de tiempos
        jugadores[i].tiempo_restante = TURNO_MAXIMO;
        jugadores[i].tiempo_bloqueado = 0;
        jugadores[i].tiempo_total_juego = 0;
        jugadores[i].tiempo_en_turno = 0;

        // Inicialización de contadores de juego
        jugadores[i].puntos = 0;
        jugadores[i].puntos_mano_actual = 0;
        jugadores[i].turnos_jugados = 0;
        jugadores[i].cartas_robadas = 0;
        jugadores[i].cartas_embonadas = 0;
        jugadores[i].grupos_formados = 0;
        jugadores[i].escaleras_formadas = 0;
        jugadores[i].memoria_asignada = 0;  // Para Parte II

        // Inicialización de la mano de cartas
        jugadores[i].mano.cartas = malloc(CARTAS_INICIALES * sizeof(carta_t));
        if (jugadores[i].mano.cartas == NULL) {
            perror("Error al asignar memoria para la mano");
            exit(EXIT_FAILURE);
        }
        jugadores[i].mano.cantidad = 0;
        jugadores[i].mano.capacidad = CARTAS_INICIALES;

        // Repartir cartas iniciales (tomando del final del mazo)
        for (int j = 0; j < CARTAS_INICIALES; j++) {
            jugadores[i].mano.cartas[j] = mazo->cartas[mazo->cantidad - 1];
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
        memset(banco->grupos[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_GRUPO);
    }

    for (int i = 0; i < MAX_ESCALERAS; i++)
    {
        banco->escaleras[i].cantidad = 0;
        memset(banco->escaleras[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_ESCALERA);
    }
}

void tabla_procesos_iniciar(tabla_procesos_t *tabla) {
    tabla->jugadores_activos = NUM_JUGADORES;
    pthread_mutex_init(&tabla->mutex, NULL);
    for (int i = 0; i < NUM_JUGADORES; i++) {
        tabla->pcbs[i].id_jugador = i + 1;
        tabla->pcbs[i].estado = LISTO;  // Estado inicial
        tabla->pcbs[i].tiempo_restante = TURNO_MAXIMO;
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
        memset(apeada->grupos[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_GRUPO);
    }

    for (int i = 0; i < MAX_ESCALERAS; i++)
    {
        apeada->escaleras[i].cantidad = 0;
        memset(apeada->escaleras[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_ESCALERA);
    }
}

void inicializar_mazo(mazo_t *mazo)
{
    const char *colores[] = {"rojo", "negro", "azul", "amarillo"};
    int index = 0;

    // Generar las 104 cartas normales (2 juegos de 1-13 en 4 colores)
    for (int k = 0; k < 2; k++)
    {
        for (int c = 0; c < 4; c++)
        {
            for (int n = 1; n <= 13; n++)
            {
                mazo->cartas[index].numero = n;
                strcpy(mazo->cartas[index].color, colores[c]);
                index++;
            }
        }
    }

    // Agregar los 4 comodines
    for (int j = 0; j < 4; j++)
    {
        mazo->cartas[index].numero = 0;
        strcpy(mazo->cartas[index].color, "comodin");
        index++;
    }

    mazo->cantidad = index;

    // Validar que el mazo tenga exactamente 108 cartas
    if (mazo->cantidad != MAX_CARTAS)
    {
        fprintf(stderr, "Error: El mazo no tiene 108 cartas. Tiene %d cartas.\n", mazo->cantidad);
        exit(EXIT_FAILURE);
    }
}

// Función para inicializar PCBs basada en la estructura actual
void inicializar_pcbs() {
    for (int i = 0; i < NUM_JUGADORES; i++) {
        // Identificación básica
        pcbs[i].id_jugador = i + 1;  // IDs comienzan en 1
        snprintf(pcbs[i].nombre, sizeof(pcbs[i].nombre), "Jugador %d", i + 1);

        // Estado del proceso/jugador
        pcbs[i].estado = LISTO;       // Estado inicial
        pcbs[i].tiempo_bloqueado = 0; // Tiempo acumulado bloqueado
        pcbs[i].memoria_asignada = 0; // Inicialmente sin memoria asignada

        // Estadísticas del juego
        pcbs[i].cartas_en_mano = CARTAS_INICIALES;
        pcbs[i].puntos = 0;
        
        // Tiempos y contadores
        pcbs[i].tiempo_restante = TURNO_MAXIMO; // Usando la constante definida
        pcbs[i].turnos_jugados = 0;
        pcbs[i].tiempo_total_juego = 0;
        pcbs[i].tiempo_en_turno = 0;

        // Acciones del juego
        pcbs[i].cartas_robadas = 0;
        pcbs[i].cartas_embonadas = 0;  // Cambiado de cartas_descartadas
        pcbs[i].grupos_formados = 0;
        pcbs[i].escaleras_formadas = 0;

        // Agregar a la cola de listos inicial (si esta función existe)
        if (agregar_a_cola_listos) {
            agregar_a_cola_listos(pcbs[i].id_jugador);
        }
    }
}

void inicializar_mapa_bits(ManejadorMemoriaMapaBits *manejador) {
    manejador->bloques_totales = TAMANO_MEMORIA / TAMANO_BLOQUE;
    manejador->bloques_libres = manejador->bloques_totales;
    
    // Inicializar todos los bits a 0 (libres)
    for (int i = 0; i < TAMANO_MAPA_BITS; i++) {
        manejador->mapa_bits[i] = 0;
    }
}
//Inicialización de la Memoria
void inicializar_memoria(GestionMemoria *gm, int tamano_total) {
    gm->memoria_total = tamano_total;
    gm->lista_bloques = (BloqueMemoria*)malloc(sizeof(BloqueMemoria));
    
    gm->lista_bloques->inicio = 0;
    gm->lista_bloques->tamano = tamano_total;
    gm->lista_bloques->disponible = 1;
    gm->lista_bloques->siguiente = NULL;
}

void mano_liberar(mano_t *mano)
{
    if (mano != NULL)
    {
        free(mano->cartas);
        mano->cartas = NULL;
        mano->cantidad = 0;
        mano->capacidad = 0;
    }
}

void liberar_jugadores()
{
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        free(jugadores[i].mano.cartas); // Liberar memoria dinámica de las cartas
        jugadores[i].mano.cartas = NULL;
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
int calcular_puntos_grupo(const carta_t grupo[], int cantidad)
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
int calcular_puntos_escalera(const carta_t escalera[], int cantidad)
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
    // Verificar que el jugador tenga al menos 3 cartas
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
                carta_t grupo[3] = {jugador->mano.cartas[i], jugador->mano.cartas[j], jugador->mano.cartas[k]};
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
                carta_t escalera[3] = {jugador->mano.cartas[i], jugador->mano.cartas[j], jugador->mano.cartas[k]};
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
        puntos += calcular_puntos_grupo(banco->grupos[i].cartas, banco->grupos[i].cantidad);
    }

    for (int i = 0; i < banco->total_escaleras; i++)
    {
        puntos += calcular_puntos_escalera(banco->escaleras[i].cartas, banco->escaleras[i].cantidad);
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
        puntos_totales += calcular_puntos_grupo(apeada->grupos[i].cartas, apeada->grupos[i].cantidad);
    }

    // Sumar puntos de todas las escaleras
    for (int i = 0; i < apeada->total_escaleras; i++)
    {
        puntos_totales += calcular_puntos_escalera(apeada->escaleras[i].cartas, apeada->escaleras[i].cantidad);
    }

    return puntos_totales;
}

// ----------------------------------------------------------------------
// Funciones de logica de apeada
// ----------------------------------------------------------------------

// Verifica si un conjunto de cartas forma un grupo válido
bool es_grupo_valido(const carta_t cartas[], int cantidad)
{
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_CARTAS_GRUPO)
    {
        return false;
    }

    int numero_base = -1;
    int comodines = 0;
    char colores_usados[MAX_CARTAS_GRUPO][MAX_COLOR] = {0};
    int colores_count = 0;

    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero == VALOR_COMODIN)
        { // Usando VALOR_COMODIN
            comodines++;
        }
        else
        {
            // Verificar que no haya cartas duplicadas (mismo número y color)
            for (int j = 0; j < colores_count; j++)
            {
                if (strcmp(cartas[i].color, colores_usados[j]) == 0)
                {
                    return false; // Color repetido en el grupo
                }
            }

            // Guardar color usado
            strncpy(colores_usados[colores_count++], cartas[i].color, MAX_COLOR - 1);

            // Establecer o verificar número base
            if (numero_base == -1)
            {
                numero_base = cartas[i].numero;
            }
            else if (cartas[i].numero != numero_base)
            {
                return false; // Números diferentes
            }
        }
    }

    // Debe haber al menos una carta no comodín
    return (numero_base != -1);
}

// Verifica si un conjunto de cartas forma una escalera válida
bool es_escalera_valida(const carta_t cartas[], int cantidad)
{
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_CARTAS_ESCALERA)
    {
        return false;
    }

    int numeros[MAX_CARTAS_ESCALERA] = {0};
    char color_base[MAX_COLOR] = "";
    int idx = 0;
    int comodines = 0;

    // Procesar cartas y verificar color consistente
    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero == VALOR_COMODIN)
        {
            comodines++;
        }
        else
        {
            // Verificar color consistente
            if (strlen(color_base) == 0)
            {
                strncpy(color_base, cartas[i].color, MAX_COLOR - 1);
            }
            else if (strcmp(color_base, cartas[i].color) != 0)
            {
                return false; // Diferente color
            }

            // Verificar que no haya números duplicados
            for (int j = 0; j < idx; j++)
            {
                if (numeros[j] == cartas[i].numero)
                {
                    return false; // Número duplicado
                }
            }

            numeros[idx++] = cartas[i].numero;
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
            if (cartas[i].numero == VALOR_COMODIN)
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

// Remueve una carta de la mano en la posición especificada
void remover_carta(mano_t *mano, int pos)
{
    if (pos >= 0 && pos < mano->cantidad)
    { // Verifica que la posición sea válida
        for (int i = pos; i < mano->cantidad - 1; i++)
        {
            mano->cartas[i] = mano->cartas[i + 1]; // Desplaza las cartas hacia la izquierda
        }
        mano->cantidad--; // Reduce la cantidad de cartas en la mano
    }
}

void remover_cartas(mano_t *mano, int indices[], int cantidad)
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
        remover_carta(mano, indices[i]);
    }
}

// Busca el mejor grupo disponible en la mano y lo añade al banco
bool buscar_mejor_grupo(mano_t *mano, apeada_t *apeada, int *puntos)
{
    int mejor_puntos = 0;
    int mejores_indices[MAX_CARTAS_GRUPO] = {-1};
    int mejor_cantidad = 0;

    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                carta_t grupo[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};

                if (es_grupo_valido(grupo, 3))
                {
                    int indices[4] = {i, j, k, -1};
                    int cantidad = 3;

                    // Verificar si podemos añadir una cuarta carta
                    for (int l = k + 1; l < mano->cantidad; l++)
                    {
                        carta_t grupo4[4] = {mano->cartas[i], mano->cartas[j],
                                             mano->cartas[k], mano->cartas[l]};
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
            nuevo_grupo->cartas[i] = mano->cartas[mejores_indices[i]];
        }

        *puntos += mejor_puntos;
        remover_cartas(mano, mejores_indices, mejor_cantidad);
        return true;
    }

    return false;
}

// Busca la mejor escalera disponible en la mano y la añade al banco
bool buscar_mejor_escalera(mano_t *mano, apeada_t *apeada, int *puntos)
{
    int mejor_puntos = 0;
    int mejores_indices[MAX_CARTAS_ESCALERA] = {-1};
    int mejor_cantidad = 0;

    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                carta_t escalera[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};

                if (es_escalera_valida(escalera, 3))
                {
                    int indices[MAX_CARTAS_ESCALERA] = {i, j, k};
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
                            carta_t escalera_ext[MAX_CARTAS_ESCALERA];
                            for (int m = 0; m < cantidad; m++)
                            {
                                escalera_ext[m] = mano->cartas[indices[m]];
                            }
                            escalera_ext[cantidad] = mano->cartas[l];

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
            nueva_escalera->cartas[i] = mano->cartas[mejores_indices[i]];
        }

        *puntos += mejor_puntos;
        remover_cartas(mano, mejores_indices, mejor_cantidad);
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
    // Primero buscamos grupos que usen números con múltiples cartas
    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        if (mano->cartas[i].numero == 0)
            continue;

        int contador = 1;
        for (int j = i + 1; j < mano->cantidad; j++)
        {
            if (mano->cartas[j].numero == mano->cartas[i].numero)
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
        if (mano->cartas[i].numero == 0)
            continue;

        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            if (strcmp(mano->cartas[i].color, mano->cartas[j].color) == 0)
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
                if (apeada->grupos[i].cartas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", apeada->grupos[i].cartas[j].numero,
                           apeada->grupos[i].cartas[j].color);
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
                if (apeada->escaleras[i].cartas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", apeada->escaleras[i].cartas[j].numero,
                           apeada->escaleras[i].cartas[j].color);
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

//Funcion para encontrar el índice de una carta en la mano
static int encontrar_indice_carta(const mano_t *mano, const carta_t *carta)
{
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (memcmp(&mano->cartas[i], carta, sizeof(carta_t)) == 0)
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
        memcpy(mano_temp.cartas, jugador->mano.cartas, jugador->mano.cantidad * sizeof(carta_t));
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
        memcpy(mano_temp.cartas, jugador->mano.cartas, jugador->mano.cantidad * sizeof(carta_t));
        mano_temp.cantidad = jugador->mano.cantidad;

        // Eliminar cartas usadas en la apeada
        for (int g = 0; g < apeada_calculada.total_grupos; g++)
        {
            for (int c = 0; c < apeada_calculada.grupos[g].cantidad; c++)
            {
                int idx = -1;
                // Buscar la carta en la mano (comparando todos los campos)
                for (int i = 0; i < mano_temp.cantidad; i++)
                {
                    if (memcmp(&mano_temp.cartas[i], &apeada_calculada.grupos[g].cartas[c], sizeof(carta_t)) == 0)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx >= 0)
                {
                    remover_carta(&mano_temp, idx);
                }
            }
        }

        // Actualizar mano real del jugador
        memcpy(jugador->mano.cartas, mano_temp.cartas, mano_temp.cantidad * sizeof(carta_t));
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

// Compara dos cartas para determinar si son iguales
bool son_cartas_iguales(const carta_t carta1, const carta_t carta2)
{
    // Comparar número y color
    return (carta1.numero == carta2.numero && strcmp(carta1.color, carta2.color) == 0);
}

void eliminar_carta_de_mano(mano_t *mano, carta_t carta)
{
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (son_cartas_iguales(mano->cartas[i], carta))
        {
            // Mover las cartas restantes una posición hacia atrás
            for (int j = i; j < mano->cantidad - 1; j++)
            {
                mano->cartas[j] = mano->cartas[j + 1];
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
        printf("%s no puede realizar apeada (no está en juego o tiene muy pocas cartas).\n",
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
            printf("%s ha realizado su primera apeada con %d puntos (mínimo requerido: %d)!\n",
                   jugador->nombre, puntos_apeada, PUNTOS_MINIMOS_APEADA);
        }
        else
        {
            printf("%s no alcanzó el mínimo de %d puntos para la primera apeada.\n",
                   jugador->nombre, PUNTOS_MINIMOS_APEADA);
            apeada_liberar(&apeada_jugador);
            return false;
        }
    }
    else if (puntos_apeada == 0)
    {
        printf("%s no tiene combinaciones válidas para apear en este turno.\n", jugador->nombre);
        apeada_liberar(&apeada_jugador);
        return false;
    }

    // --- Nuevo: Eliminar cartas usadas en la apeada de la mano del jugador ---
    for (int i = 0; i < apeada_jugador.total_grupos; i++)
    {
        grupo_t *grupo = &apeada_jugador.grupos[i];
        for (int j = 0; j < grupo->cantidad; j++)
        {
            eliminar_carta_de_mano(&jugador->mano, grupo->cartas[j]);
        }
    }

    for (int i = 0; i < apeada_jugador.total_escaleras; i++)
    {
        escalera_t *escalera = &apeada_jugador.escaleras[i];
        for (int j = 0; j < escalera->cantidad; j++)
        {
            eliminar_carta_de_mano(&jugador->mano, escalera->cartas[j]);
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

    // Liberar recursos (solo estructuras, no las cartas transferidas)
    free(apeada_jugador.grupos);
    free(apeada_jugador.escaleras);

    return true;
}

// ----------------------------------------------------------------------
// Funciones para embonar
// ----------------------------------------------------------------------

// Verifica si una carta puede ser agregada a un grupo existente
bool puede_embonar_grupo(const carta_t *carta, const grupo_t *grupo)
{
    // Si el grupo ya tiene el máximo de cartas
    if (grupo->cantidad >= MAX_CARTAS_GRUPO)
        return false;

    // Buscar el número base del grupo (ignorando comodines)
    int numero_base = -1;
    for (int i = 0; i < grupo->cantidad; i++)
    {
        if (grupo->cartas[i].numero != 0)
        {
            numero_base = grupo->cartas[i].numero;
            break;
        }
    }

    // Si todas son comodines, cualquier carta puede unirse
    if (numero_base == -1)
        return true;

    // La carta debe coincidir con el número base o ser comodín
    return (carta->numero == numero_base || carta->numero == 0);
}

// Verifica si una carta puede ser agregada a una escalera existente
bool puede_embonar_escalera(const carta_t *carta, const escalera_t *escalera)
{
    // Si la escalera ya tiene el máximo de cartas
    if (escalera->cantidad >= MAX_CARTAS_ESCALERA)
        return false;

    // Buscar color base y valores existentes
    char color_base[MAX_COLOR] = "";
    int valores[MAX_CARTAS_ESCALERA];
    int num_valores = 0;
    int comodines = 0;

    for (int i = 0; i < escalera->cantidad; i++)
    {
        if (escalera->cartas[i].numero == 0)
        {
            comodines++;
        }
        else
        {
            if (strlen(color_base) == 0)
            {
                strcpy(color_base, escalera->cartas[i].color);
            }
            valores[num_valores++] = escalera->cartas[i].numero;
        }
    }

    // Si no hay cartas no-comodín, cualquier carta del mismo color puede unirse
    if (num_valores == 0)
    {
        return (strcmp(carta->color, color_base) == 0 || strlen(color_base) == 0);
    }

    // Verificar color
    if (strcmp(carta->color, color_base) != 0 && carta->numero != 0)
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

    // Verificar si la carta puede encajar en la secuencia
    if (carta->numero != 0)
    { // No es comodín
        // Verificar si el número ya existe
        for (int i = 0; i < num_valores; i++)
        {
            if (valores[i] == carta->numero)
            {
                return false;
            }
        }

        // Verificar extremos
        if (carta->numero == valores[0] - 1 || carta->numero == valores[num_valores - 1] + 1)
        {
            return true;
        }

        // Verificar huecos internos
        for (int i = 0; i < num_valores - 1; i++)
        {
            if (carta->numero > valores[i] && carta->numero < valores[i + 1])
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
bool mover_comodin_para_embonar(banco_de_apeadas_t *banco, const carta_t *carta)
{
    // Buscar comodines en grupos
    for (int g = 0; g < banco->total_grupos; g++)
    {
        grupo_t *grupo = &banco->grupos[g];

        // Buscar comodines en este grupo
        for (int i = 0; i < grupo->cantidad; i++)
        {
            if (grupo->cartas[i].numero == 0)
            { // Es comodín
                // Intentar mover este comodín a una escalera
                for (int e = 0; e < banco->total_escaleras; e++)
                {
                    escalera_t *escalera = &banco->escaleras[e];

                    if (escalera->cantidad < MAX_CARTAS_ESCALERA)
                    {
                        // Mover comodín del grupo a la escalera
                        carta_t comodin = grupo->cartas[i];

                        // Eliminar comodín del grupo
                        for (int j = i; j < grupo->cantidad - 1; j++)
                        {
                            grupo->cartas[j] = grupo->cartas[j + 1];
                        }
                        grupo->cantidad--;

                        // Agregar comodín a la escalera
                        escalera->cartas[escalera->cantidad++] = comodin;

                        // Ahora intentar embonar la carta original en el grupo
                        if (puede_embonar_grupo(carta, grupo))
                        {
                            return true;
                        }

                        // Si no funciona, revertir el movimiento
                        // Devolver comodín al grupo
                        grupo->cartas[grupo->cantidad++] = comodin;
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
            if (escalera->cartas[i].numero == 0)
            { // Es comodín
                // Intentar mover este comodín a un grupo
                for (int g = 0; g < banco->total_grupos; g++)
                {
                    grupo_t *grupo = &banco->grupos[g];

                    if (grupo->cantidad < MAX_CARTAS_GRUPO)
                    {
                        // Mover comodín de la escalera al grupo
                        carta_t comodin = escalera->cartas[i];

                        // Eliminar comodín de la escalera
                        for (int j = i; j < escalera->cantidad - 1; j++)
                        {
                            escalera->cartas[j] = escalera->cartas[j + 1];
                        }
                        escalera->cantidad--;

                        // Agregar comodín al grupo
                        grupo->cartas[grupo->cantidad++] = comodin;

                        // Ahora intentar embonar la carta original en la escalera
                        if (puede_embonar_escalera(carta, escalera))
                        {
                            return true;
                        }

                        // Si no funciona, revertir el movimiento
                        // Devolver comodín a la escalera
                        escalera->cartas[escalera->cantidad++] = comodin;
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

// Función para intentar embonar una carta en un grupo o escalera
bool intentar_embonar_carta(carta_t *carta, banco_de_apeadas_t *banco)
{
    // Intentar embonar en grupos
    for (int i = 0; i < banco->total_grupos; i++)
    {
        if (puede_embonar_grupo(carta, &banco->grupos[i]))
        {
            banco->grupos[i].cartas[banco->grupos[i].cantidad++] = *carta;
            return true;
        }
    }

    // Intentar embonar en escaleras
    for (int i = 0; i < banco->total_escaleras; i++)
    {
        if (puede_embonar_escalera(carta, &banco->escaleras[i]))
        {
            banco->escaleras[i].cartas[banco->escaleras[i].cantidad++] = *carta;
            return true;
        }
    }

    // Si no se pudo embonar, intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(carta, 1))
    {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->cartas[0] = *carta;
        nuevo_grupo->cantidad = 1;
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(carta, 1))
    {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->cartas[0] = *carta;
        nueva_escalera->cantidad = 1;
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Función para intentar mover comodines y luego embonar la carta
bool intentar_mover_comodin_y_embonar(carta_t *carta, banco_de_apeadas_t *banco)
{
    if (mover_comodin_para_embonar(banco, carta))
    {
        return intentar_embonar_carta(carta, banco);
    }
    return false;
}

// Función principal para embonar una carta del jugador al banco
bool embonar_carta(jugador_t *jugador, banco_de_apeadas_t *banco, int indice_carta)
{
    if (indice_carta < 0 || indice_carta >= jugador->mano.cantidad)
    {
        return false; // Índice inválido
    }

    carta_t carta = jugador->mano.cartas[indice_carta];

    // Intentar embonar directamente
    if (intentar_embonar_carta(&carta, banco))
    {
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    // Intentar mover comodines y luego embonar
    if (intentar_mover_comodin_y_embonar(&carta, banco))
    {
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    // Intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(&carta, 1))
    {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->cartas[0] = carta;
        nuevo_grupo->cantidad = 1;
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(&carta, 1))
    {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->cartas[0] = carta;
        nueva_escalera->cantidad = 1;
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Verifica si una carta puede embonar en algún grupo o escalera del banco
bool existe_embon_posible_aux(const jugador_t *jugador, const banco_de_apeadas_t *banco)
{
    // 1. Verificar embón en grupos/escaleras existentes
    for (int i = 0; i < jugador->mano.cantidad; i++)
    {
        const carta_t *carta = &jugador->mano.cartas[i];

        // Verificar grupos
        for (int g = 0; g < banco->total_grupos; g++)
        {
            if (puede_embonar_grupo(carta, &banco->grupos[g]))
            {
                return true;
            }
        }

        // Verificar escaleras
        for (int e = 0; e < banco->total_escaleras; e++)
        {
            if (puede_embonar_escalera(carta, &banco->escaleras[e]))
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
            const carta_t *carta = &jugador->mano.cartas[i];

            // Crear nuevo grupo (necesita al menos 2 cartas iguales + comodines)
            if (banco->total_grupos < MAX_GRUPOS)
            {
                int contador = 0;
                for (int j = 0; j < jugador->mano.cantidad; j++)
                {
                    if (i != j && (jugador->mano.cartas[j].numero == carta->numero ||
                                   jugador->mano.cartas[j].numero == 0))
                    {
                        contador++;
                        if (contador >= 2)
                            return true; // 2 cartas iguales o 1 + comodín
                    }
                }
            }

            // Crear nueva escalera (necesita cartas consecutivas del mismo color)
            if (banco->total_escaleras < MAX_ESCALERAS)
            {
                for (int j = 0; j < jugador->mano.cantidad; j++)
                {
                    if (i != j && strcmp(jugador->mano.cartas[j].color, carta->color) == 0)
                    {
                        int diff = abs(jugador->mano.cartas[j].numero - carta->numero);
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
        if (mano->cartas[i].numero == 0)
        {
            puntos += 20;
        }
        // Cartas normales valen su valor numérico
        else
        {
            puntos += mano->cartas[i].numero;
        }
    }
    return puntos;
}

// Verifica si un jugador ha ganado (se quedó sin cartas)
bool jugador_ha_ganado(const jugador_t *jugador)
{
    return jugador->mano.cantidad == 0;
}

// Determina el índice del jugador ganador
int determinar_ganador(jugador_t *jugadores, int num_jugadores, bool mazo_vacio)
{
    // Primero verificar si algún jugador se quedó sin cartas
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

void barajar_mazo(mazo_t *mazo)
{
    srand(time(NULL));
    for (int i = 0; i < mazo->cantidad; i++)
    {
        int j = rand() % mazo->cantidad;
        carta_t temp = mazo->cartas[i];
        mazo->cartas[i] = mazo->cartas[j];
        mazo->cartas[j] = temp;
    }
}

void robar_carta(jugador_t *jugador) {
    if (mazo.cantidad > 0) {
        carta_t nueva = mazo.cartas[--mazo.cantidad];
        agregar_carta(&jugador->mano, nueva);

        // Acceder a la página correspondiente a la carta
        acceder_pagina(nueva.numero);

        mostrar_robo_carta(&nueva, false);
        jugador->carta_agregada = true;
    } else {
        printf("El mazo está vacío. No se puede robar una carta.\n");
    }
}

// ----------------------------------------------------------------------
// Funciones para la Mano
// ----------------------------------------------------------------------
void agregar_carta(mano_t *mano, carta_t carta)
{
    if (mano->cantidad < mano->capacidad)
    {
        mano->cartas[mano->cantidad] = carta;
        mano->cantidad++;
    }
    else
    {
        // Si la mano está llena, aumentar la capacidad y reasignar memoria
        mano->capacidad *= 2;
        mano->cartas = realloc(mano->cartas, sizeof(carta_t) * mano->capacidad);
        if (mano->cartas == NULL)
        {
            fprintf(stderr, "Error al reasignar memoria para la mano\n");
            exit(EXIT_FAILURE);
        }
        mano->cartas[mano->cantidad] = carta;
        mano->cantidad++;
    }
}

void mostrar_mano(mano_t *mano)
{
    printf("Cartas en mano (%d):\n", mano->cantidad);
    for (int i = 0; i < mano->cantidad; i++)
    {
        if (mano->cartas[i].numero == 0)
        {
            printf("Comodin\n");
        }
        else
        {
            printf("%d de %s\n", mano->cartas[i].numero, mano->cartas[i].color);
        }
    }
}

void finalizar_turno(jugador_t *jugador)
{
    jugador->carta_agregada = false; // Reinicia la bandera para el próximo turno
}

// ----------------------------------------------------------------------
// Funciones para Manipular el mapa de Bits
// ----------------------------------------------------------------------

// Establecer un bit como ocupado (1)
void establecer_bit(ManejadorMemoriaMapaBits *manejador, int posicion) {
    int indice_byte = posicion / 8;
    int indice_bit = posicion % 8;
    manejador->mapa_bits[indice_byte] |= (1 << indice_bit);
    manejador->bloques_libres--;
}

// Limpiar un bit (liberar, poner a 0)
void limpiar_bit(ManejadorMemoriaMapaBits *manejador, int posicion) {
    int indice_byte = posicion / 8;
    int indice_bit = posicion % 8;
    manejador->mapa_bits[indice_byte] &= ~(1 << indice_bit);
    manejador->bloques_libres++;
}

// Verificar si un bit está ocupado
int probar_bit(ManejadorMemoriaMapaBits *manejador, int posicion) {
    int indice_byte = posicion / 8;
    int indice_bit = posicion % 8;
    return (manejador->mapa_bits[indice_byte] & (1 << indice_bit)) != 0;
}

// Asignar bloques contiguos usando mapa de bits
int asignar_memoria_mapa(ManejadorMemoriaMapaBits *manejador, int tamano_requerido) {
    int bloques_necesarios = (tamano_requerido + TAMANO_BLOQUE - 1) / TAMANO_BLOQUE;
    int libres_consecutivos = 0;
    int bloque_inicio = -1;
    
    if (bloques_necesarios > manejador->bloques_libres) {
        return -1;  // No hay suficiente memoria
    }
    
    // Buscar bloques contiguos libres
    for (int i = 0; i < manejador->bloques_totales; i++) {
        if (!probar_bit(manejador, i)) {
            if (libres_consecutivos == 0) {
                bloque_inicio = i;
            }
            libres_consecutivos++;
            
            if (libres_consecutivos == bloques_necesarios) {
                // Marcar los bloques como ocupados
                for (int j = bloque_inicio; j < bloque_inicio + bloques_necesarios; j++) {
                    establecer_bit(manejador, j);
                }
                return bloque_inicio * TAMANO_BLOQUE;  // Devuelve la dirección base
            }
        } else {
            libres_consecutivos = 0;
            bloque_inicio = -1;
        }
    }
    
    return -1;  // No se encontró espacio contiguo suficiente
}

// Liberar bloques de memoria
void liberar_memoria_mapa(ManejadorMemoriaMapaBits *manejador, int direccion_base, int tamano) {
    int bloque_inicio = direccion_base / TAMANO_BLOQUE;
    int bloques_a_liberar = (tamano + TAMANO_BLOQUE - 1) / TAMANO_BLOQUE;
    
    for (int i = bloque_inicio; i < bloque_inicio + bloques_a_liberar; i++) {
        if (probar_bit(manejador, i)) {
            limpiar_bit(manejador, i);
        }
    }
}

// Mostrar el estado actual del mapa de bits
void imprimir_mapa_bits(ManejadorMemoriaMapaBits *manejador) {
    printf("Estado de la memoria (0=libre, 1=ocupado):\n");
    printf("Bloques totales: %d, Bloques libres: %d\n", 
           manejador->bloques_totales, manejador->bloques_libres);
    printf("\n");
}

// ----------------------------------------------------------------------
// Funciones para la Gestión de Memoria (Lista Ligada)
// ----------------------------------------------------------------------

//Algoritmo de Mejor Ajuste
BloqueMemoria* mejor_ajuste(GestionMemoria *gm, int tamano_solicitado) {
    BloqueMemoria *actual = gm->lista_bloques;
    BloqueMemoria *mejor_bloque = NULL;
    int menor_diferencia = gm->memoria_total + 1; // Valor mayor al máximo posible
    
    while (actual != NULL) {
        if (actual->disponible && actual->tamano >= tamano_solicitado) {
            int diferencia = actual->tamano - tamano_solicitado;
            if (diferencia < menor_diferencia) {
                menor_diferencia = diferencia;
                mejor_bloque = actual;
            }
        }
        actual = actual->siguiente;
    }
    
    return mejor_bloque;
}

//Asignación de Memoria
int asignar_memoria_lista(GestionMemoria *gm, int tamano_solicitado) {
    BloqueMemoria *bloque = mejor_ajuste(gm, tamano_solicitado);
    
    if (bloque == NULL) {
        return -1; // No hay memoria disponible
    }
    
    // Si el bloque es exacto
    if (bloque->tamano == tamano_solicitado) {
        bloque->disponible = 0;
        return bloque->inicio;
    } else {
        // Crear nuevo bloque para el espacio sobrante
        BloqueMemoria *nuevo_bloque = (BloqueMemoria*)malloc(sizeof(BloqueMemoria));
        nuevo_bloque->inicio = bloque->inicio + tamano_solicitado;
        nuevo_bloque->tamano = bloque->tamano - tamano_solicitado;
        nuevo_bloque->disponible = 1;
        nuevo_bloque->siguiente = bloque->siguiente;
        
        // Modificar el bloque actual
        bloque->tamano = tamano_solicitado;
        bloque->disponible = 0;
        bloque->siguiente = nuevo_bloque;
        
        return bloque->inicio;
    }
}

//Liberación de Memoria
void liberar_memoria_lista(GestionMemoria *gm, int direccion, int tamano) {
    BloqueMemoria *actual = gm->lista_bloques;
    BloqueMemoria *anterior = NULL;
    
    // Buscar el bloque a liberar
    while (actual != NULL && actual->inicio != direccion) {
        anterior = actual;
        actual = actual->siguiente;
    }
    
    if (actual != NULL && !actual->disponible) {
        actual->disponible = 1;
        
        // Fusionar con bloque anterior si está libre
        if (anterior != NULL && anterior->disponible) {
            anterior->tamano += actual->tamano;
            anterior->siguiente = actual->siguiente;
            free(actual);
            actual = anterior;
        }
        
        // Fusionar con bloque siguiente si está libre
        if (actual->siguiente != NULL && actual->siguiente->disponible) {
            actual->tamano += actual->siguiente->tamano;
            BloqueMemoria *temp = actual->siguiente;
            actual->siguiente = temp->siguiente;
            free(temp);
        }
    }
}


//inicializar la memoria virtual

// ...existing code...

void inicializar_memoria_virtual() {
    for (int i = 0; i < NUM_PAGINAS; i++) {
        memoria_virtual.paginas[i].id_pagina = i;
        memoria_virtual.paginas[i].en_memoria = false;
        memoria_virtual.paginas[i].bit_segunda_oportunidad = false;
    }
    memoria_virtual.puntero_reemplazo = 0;
}

//Manejar el fallo de pagina

void manejar_fallo_pagina(int id_pagina) {
    while (true) {
        pagina_t *pagina_actual = &memoria_virtual.paginas[memoria_virtual.puntero_reemplazo];

        if (!pagina_actual->bit_segunda_oportunidad) {
            // Reemplazar esta página
            printf("Reemplazando página %d con página %d\n", pagina_actual->id_pagina, id_pagina);
            pagina_actual->en_memoria = false;

            // Actualizar la nueva página
            memoria_virtual.paginas[id_pagina].en_memoria = true;
            memoria_virtual.paginas[id_pagina].bit_segunda_oportunidad = true;

            // Mover el puntero al siguiente marco
            memoria_virtual.puntero_reemplazo = (memoria_virtual.puntero_reemplazo + 1) % NUM_PAGINAS;
            return;
        } else {
            // Dar una segunda oportunidad
            pagina_actual->bit_segunda_oportunidad = false;
        }

        // Avanzar el puntero circular
        memoria_virtual.puntero_reemplazo = (memoria_virtual.puntero_reemplazo + 1) % NUM_PAGINAS;
    }
}

// Acceder a paginas

void acceder_pagina(int id_pagina) {
    if (memoria_virtual.paginas[id_pagina].en_memoria) {
        // Página está en memoria, actualizar bit de segunda oportunidad
        printf("Acceso a página %d: HIT\n", id_pagina);
        memoria_virtual.paginas[id_pagina].bit_segunda_oportunidad = true;
    } else {
        // Página no está en memoria, manejar fallo de página
        printf("Acceso a página %d: MISS\n", id_pagina);
        manejar_fallo_pagina(id_pagina);
    }
}

void mostrar_estado_memoria() {
    printf("\n=== Estado de la Memoria Virtual ===\n");
    for (int i = 0; i < NUM_PAGINAS; i++) {
        printf("Página %d: %s, Bit Segunda Oportunidad: %d\n",
               memoria_virtual.paginas[i].id_pagina,
               memoria_virtual.paginas[i].en_memoria ? "En Memoria" : "En Disco",
               memoria_virtual.paginas[i].bit_segunda_oportunidad);
    }
    printf("────────────────────────────────────\n");
}

//Visualización del Estado de la Memoria
void mostrar_memoria(GestionMemoria *gm) {
    BloqueMemoria *actual = gm->lista_bloques;
    printf("Estado de la memoria:\n");
    printf("Total: %d unidades\n", gm->memoria_total);
    printf("--------------------------------\n");
    
    while (actual != NULL) {
        printf("[%d-%d] %s (%d unidades)\n", 
               actual->inicio, 
               actual->inicio + actual->tamano - 1,
               actual->disponible ? "Libre" : "Ocupado",
               actual->tamano);
        actual = actual->siguiente;
    }
    printf("--------------------------------\n");
}

// ----------------------------------------------------------------------
// Funciones de Inicialización de Jugadores y PCB
// ----------------------------------------------------------------------

void escribir_pcb(pcb_t jugador) {
    char filename[30];
    sprintf(filename, "PCB_Jugador%d.txt", jugador.id_jugador);

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        printf("Error al abrir %s\n", filename);
        return;
    }

    // Convertir el estado a texto
    const char *estado_str;
    switch(jugador.estado) {
        case LISTO: estado_str = "LISTO"; break;
        case EJECUTANDO: estado_str = "EJECUTANDO"; break;
        case BLOQUEADO: estado_str = "BLOQUEADO"; break;
        default: estado_str = "DESCONOCIDO";
    }

    // Escribir los datos del PCB en el archivo
    fprintf(file, "=== PCB del Jugador ===\n");
    fprintf(file, "ID: %d\n", jugador.id_jugador);
    fprintf(file, "Nombre: %s\n", jugador.nombre);
    fprintf(file, "Estado: %s\n", estado_str);
    fprintf(file, "\n=== Estadísticas del Juego ===\n");
    fprintf(file, "Cartas en Mano: %d\n", jugador.cartas_en_mano);
    fprintf(file, "Puntos Acumulados: %d\n", jugador.puntos);
    fprintf(file, "Turnos Jugados: %d\n", jugador.turnos_jugados);
    fprintf(file, "Cartas Robadas: %d\n", jugador.cartas_robadas);
    fprintf(file, "Cartas Embonadas: %d\n", jugador.cartas_embonadas);
    fprintf(file, "Grupos Formados: %d\n", jugador.grupos_formados);
    fprintf(file, "Escaleras Formadas: %d\n", jugador.escaleras_formadas);
    fprintf(file, "\n=== Tiempos ===\n");
    fprintf(file, "Tiempo Restante (turno actual): %d\n", jugador.tiempo_restante);
    fprintf(file, "Tiempo Total en Turnos: %d\n", jugador.tiempo_en_turno);
    fprintf(file, "Tiempo Bloqueado: %d\n", jugador.tiempo_bloqueado);
    fprintf(file, "Tiempo Total de Juego: %d\n", jugador.tiempo_total_juego);
    fprintf(file, "\n=== Recursos ===\n");
    fprintf(file, "Memoria Asignada: %d KB\n", jugador.memoria_asignada);

    fclose(file);
    printf("PCB del jugador %d guardado en %s\n", jugador.id_jugador, filename);
}

void actualizar_y_escribir_pcb(pcb_t *pcb, jugador_t *jugador)
{
    // Guardamos la cantidad de cartas anterior
    int cartas_anteriores = pcb->cartas_en_mano;

    // Actualizar información básica del jugador
    pcb->id_jugador = jugador->id;
    strcpy(pcb->nombre, jugador->nombre);
    pcb->cartas_en_mano = jugador->mano.cantidad;

    // Actualizar puntos acumulados
    pcb->puntos = calcular_puntos_mano(&jugador->mano);

    // Verificar si robó carta(s)
    if (jugador->mano.cantidad > cartas_anteriores)
    {
        int robadas = jugador->mano.cantidad - cartas_anteriores;
        pcb->cartas_robadas += robadas;
    }

    // Determinar el estado del jugador
    if (!jugador->en_juego)
    {
        pcb->estado = BLOQUEADO;
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

    // Si está bloqueado, aumentar tiempo bloqueado
    if (pcb->estado == BLOQUEADO)
    {
        pcb->tiempo_bloqueado++;
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

    // Encabezado de la tabla
    fprintf(file, "ID\tNombre\tCartas\tPuntos\tEstado\tTurnos\tRobadas\tGrupos\tEscaleras\tTiempoJ\tTiempoBloq\n");

    for (int i = 0; i < num_jugadores; i++)
    {
        // Convertir el estado a texto
        const char *estado_str = (jugadores[i].estado == LISTO) ? "LISTO" :
                                 (jugadores[i].estado == EJECUTANDO) ? "EJECUTANDO" : "BLOQUEADO";

        // Escribir los datos del jugador en la tabla
        fprintf(file, "%d\t%s\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n",
                jugadores[i].id_jugador,
                jugadores[i].nombre,
                jugadores[i].cartas_en_mano,
                jugadores[i].puntos,
                estado_str,
                jugadores[i].turnos_jugados,
                jugadores[i].cartas_robadas,
                jugadores[i].grupos_formados,
                jugadores[i].escaleras_formadas,
                jugadores[i].tiempo_total_juego,
                jugadores[i].tiempo_bloqueado);
    }

    fclose(file);
}

// ----------------------------------------------------------------------
// Función para repartir cartas entre jugadores y mostrar banco de apeadas
// ----------------------------------------------------------------------
void repartir_cartas(jugador_t jugadores[], int num_jugadores, mazo_t *mazo)
{
    int cartas_por_jugador = (mazo->cantidad * 2) / (3 * num_jugadores);
    int index = 0;

    for (int i = 0; i < num_jugadores; i++)
    {
        for (int j = 0; j < cartas_por_jugador; j++)
        {
            agregar_carta(&(jugadores[i].mano), mazo->cartas[index]);
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
        printf("El banco de apeadas está vacío.\n");
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
                if (banco->grupos[i].cartas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", banco->grupos[i].cartas[j].numero, banco->grupos[i].cartas[j].color);
                }
            }
            printf("\n");
        }
    }
    else
    {
        printf("No hay grupos en el banco.\n");
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
                if (banco->escaleras[i].cartas[j].numero == 0)
                {
                    printf("[Comodín] ");
                }
                else
                {
                    printf("[%d %s] ", banco->escaleras[i].cartas[j].numero, banco->escaleras[i].cartas[j].color);
                }
            }
            printf("\n");
        }
    }
    else
    {
        printf("No hay escaleras en el banco.\n");
    }

    printf("────────────────────────────\n");
}

// ----------------------------------------------------------------------
// Funciones de Concurrencia y Manejo de Turnos
// ----------------------------------------------------------------------

// Función auxiliar para manejar al ganador
void manejar_ganador(int ganador)
{
    printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);

    // Actualizar estadísticas de todos los jugadores
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        if (i != ganador)
        {
            pcbs[i].cartas_en_mano = jugadores[i].mano.cantidad; // Actualizar cartas restantes
        }
        escribir_pcb(pcbs[i]); // Guardar el PCB actualizado
    }

    // Marcar el juego como terminado
    pthread_mutex_lock(&mutex_terminacion);
    juego_terminado = true;
    pthread_mutex_unlock(&mutex_terminacion);

    pthread_cond_broadcast(&cond_turno); // Notificar a todos los hilos
}

// Función auxiliar para manejar el cambio de política de planificación
void manejar_cambio_politica()
{
    if (kbhit())
    {
        char c = getchar();
        if (c == 'F' || c == 'f')
        {
            modo = 'F';
            printf("\n✔ Política cambiada a FCFS: Turnos completos en orden de llegada\n");
        }
        else if (c == 'R' || c == 'r')
        {
            modo = 'R';
            printf("\n✔ Política cambiada a Round Robin: Quantum de %d segundos\n", QUANTUM);
        }
        else
        {
            printf("\n➜ Entrada inválida. Manteniendo política actual: %s\n",
                   (modo == 'F') ? "FCFS" : "Round Robin");
        }
    }
}

/// Hilo principal que controla el estado global del juego
void *hilo_juego_func(void *arg)
{
    while (!juego_terminado)
    {
        pthread_mutex_lock(&mutex);

        // Detectar ganador continuamente
        int ganador = determinar_ganador(jugadores, NUM_JUGADORES, mazo.cantidad == 0);
        if (ganador != -1)
        {
            manejar_ganador(ganador);
            pthread_mutex_unlock(&mutex);
            break; // Salir del bucle principal
        }

        // Cambiar política de planificación con teclas F/R
        manejar_cambio_politica();

        pthread_mutex_unlock(&mutex);
        sleep(1); // Revisar estado cada segundo
    }

    return NULL;
}

// Hilo planificador gestiona turnos de los jugadores
void *planificador(void *arg) {
    hilo_control_t *control = (hilo_control_t *)arg;
    struct timespec sleep_time = {0, 100000000}; // 100ms

    while (!*(control->terminar_flag)) {
        pthread_mutex_lock(&mutex); // Usar el mutex general del juego

        if (proceso_en_ejecucion == -1) {
            int siguiente = siguiente_turno();

            // Verificar si el jugador está LISTO (no bloqueado)
            if (siguiente != -1 && pcbs[siguiente - 1].estado == LISTO) {
                proceso_en_ejecucion = siguiente;
                jugador_t *jugador = &jugadores[siguiente - 1];
                pcbs[siguiente - 1].estado = EJECUTANDO;

                // Iniciar turno del jugador
                pthread_t hilo_jugador;
                pthread_create(&hilo_jugador, NULL, jugador_thread, jugador);
                pthread_detach(hilo_jugador);

                // Esperar fin del turno (quantum o acción)
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += QUANTUM;
                pthread_cond_timedwait(control->cond, &mutex, &timeout);

                proceso_en_ejecucion = -1;

                // --- REINSERTAR SOLO SI NO FUE BLOQUEADO DURANTE EL TURNO ---
                if (modo == 'R' && pcbs[siguiente - 1].estado == EJECUTANDO) {
                    agregar_a_cola_listos(siguiente);
                }
            }
        }

        pthread_mutex_unlock(&mutex);
        nanosleep(&sleep_time, NULL); // Reducir uso de CPU
    }
    return NULL;
}

// Función para bloquear jugador
void bloquear_jugador(int id_jugador, int tiempo) {
    // ¡No usar pthread_mutex_lock aquí! (Ya está bloqueado por el llamador)
    if (num_bloqueados < NUM_JUGADORES) {        
        jugadores[id_jugador - 1].estado = BLOQUEADO;
        pcbs[id_jugador - 1].estado = BLOQUEADO;
        pcbs[id_jugador - 1].tiempo_bloqueado = tiempo * 10; // Convertir segundos a décimas

        cola_bloqueados[num_bloqueados++] = id_jugador;
        printf("[BLOQUEO] %s bloqueado por %d segundos.\n", 
               jugadores[id_jugador - 1].nombre, tiempo);
    }
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
        printf("1. Robar carta\n2. Hacer apeada\n3. Embonar carta\n");
        printf("4. Mostrar banco\n5. Pasar turno\n");
        printf("Seleccione (1-5): ");
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

            if (modo == 'R' && elapsed >= QUANTUM)
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
                carta_t nueva = mazo.cartas[--mazo.cantidad];
                agregar_carta(&jugador->mano, nueva);
                mostrar_robo_carta(&nueva, false);
                jugador->carta_agregada = true;
                turno_activo = false;
                hizo_accion = true;
            }
            break;

        case 2:
            if (puede_hacer_apeada(jugador))
            {
                apeada_t apeada = calcular_mejor_apeada_aux(jugador);
                if (realizar_apeada_optima(jugador, &banco_apeadas))
                {
                    printf("¡Apeada exitosa!\n");
                    mostrar_apeada(&apeada);
                    hizo_accion = true;
                    if (modo == 'F')
                    {
                        turno_activo = false;
                    }
                }
                apeada_liberar(&apeada);
            }
            else
            {
                printf("No tienes combinaciones válidas para apear\n");
            }
            break;

        case 3:
            if (jugador->mano.cantidad > 0)
            {
                mostrar_mano(&jugador->mano);
                printf("Seleccione carta (1-%d): ", jugador->mano.cantidad);
                fflush(stdout);

                struct pollfd carta_poll = {STDIN_FILENO, POLLIN, 0};
                char input2[10] = {0};
                while (true)
                {
                    int ret = poll(&carta_poll, 1, 200);
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
                            if (embonar_carta(jugador, &banco_apeadas, idx))
                            {
                                printf("¡Carta embonada con éxito!\n");
                                hizo_accion = true;
                                if (modo == 'F')
                                {
                                    turno_activo = false;
                                }
                            }
                            else
                            {
                                printf("No se pudo embonar la carta\n");
                            }
                        }
                        else
                        {
                            printf("Índice inválido\n");
                        }
                        break;
                    }
                }
            }
            else
            {
                printf("No tienes cartas para embonar\n");
            }
            break;

        case 4:
            mostrar_banco(&banco_apeadas);
            break;

            case 5:
            if (mazo.cantidad > 0)
            {
                carta_t nueva = mazo.cartas[--mazo.cantidad];
                agregar_carta(&jugador->mano, nueva);
                mostrar_robo_carta(&nueva, false);
                mi_pcb->cartas_en_mano = jugador->mano.cantidad;
                mi_pcb->cartas_robadas++;
                printf("Has pasado el turno y robado una carta.\n");
            }
            else
            {
                printf("No se puede robar una carta: el mazo está vacío.\n");
            }
        
            turno_activo = false;
            hizo_accion = true;
            break;

        default:
            if (opcion != -1)
            {
                printf("Opción inválida. Por favor seleccione 1-5\n");
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
        if (modo == 'R' && elapsed >= QUANTUM)
        {
            printf("\n¡Quantum completado!\n");
            turno_activo = false;
        }

        jugador->tiempo_restante = QUANTUM - (int)elapsed;

        if (!turno_activo)
        {
            printf("\n=== Fin de turno de %s ===\n", jugador->nombre);
            srand(time(NULL)); // Inicializa la semilla aleatoria una sola vez

            int tam_random = (rand() % 151) + 50; // número aleatorio entre 50 y 200
            int direccion1 = asignar_memoria_mapa(&manejador, tam_random);
        
            printf("\nAsignados %d bytes en mapa en la direccion: %d\n", tam_random, direccion1);
            imprimir_mapa_bits(&manejador);

            int dir1 = asignar_memoria_lista(&gm, tam_random);
            printf("\nAsignadas unidades en lista en direccion: %d\n", dir1);
            mostrar_memoria(&gm);
        }
    }

        // Verificar condiciones de victoria
        if (jugador->mano.cantidad == 0)
        {
            printf("¡%s se ha quedado sin cartas y gana el juego!\n", jugador->nombre);
            return NULL;
        }

        if (mazo.cantidad == 0)
        {
            printf("¡El mazo se ha agotado!\n");
            int ganador = determinar_ganador(jugadores, NUM_JUGADORES, true);
            printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);
            return NULL;
        }

        if (!juego_terminado) {
        int tiempo_bloqueo = (rand() % 11) + 10; // 10-20 seconds
        bloquear_jugador(jugador->id, tiempo_bloqueo);
    }
    
    return NULL;
}

// Función agregar_a_cola_listos (modificada)
void agregar_a_cola_listos(int id_jugador) {
    pthread_mutex_lock(&mutex);

    // Solo agregar si está LISTO y no está ya en la cola
    if (pcbs[id_jugador - 1].estado == LISTO) {
        bool existe = false;
        for (int i = frente; i != final; i = (i + 1) % NUM_JUGADORES) {
            if (cola_listos[i] == id_jugador) {
                existe = true;
                break;
            }
        }
        if (!existe) {
            cola_listos[final] = id_jugador;
            final = (final + 1) % NUM_JUGADORES;
        }
    }

    pthread_mutex_unlock(&mutex);
}

// Hilo que verifica jugadores bloqueados periódicamente
void *verificar_bloqueados(void *arg) {
    hilo_control_t *control = (hilo_control_t *)arg;
    struct timespec sleep_time = {0, 100000000}; // 100ms (0.1s)
    
    while (1) {
        pthread_mutex_lock(&mutex);
        
        if (*(control->terminar_flag)) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        
        // Procesar todos los bloqueados CADA 100ms
        for (int i = 0; i < num_bloqueados; i++) {
            int id = cola_bloqueados[i];
            jugador_t *jugador = &jugadores[id - 1];
            
            if (jugador->tiempo_bloqueado > 0) {
                jugador->tiempo_bloqueado--;  // Decrementar 1 unidad (0.1s)
                
                if (jugador->tiempo_bloqueado <= 0) {
                    // Mover a LISTOS
                    jugador->estado = LISTO;
                    pcbs[id - 1].estado = LISTO;
                    agregar_a_cola_listos(id);
                    
                    // Eliminar de BLOQUEADOS
                    for (int j = i; j < num_bloqueados - 1; j++) {
                        cola_bloqueados[j] = cola_bloqueados[j + 1];
                    }
                    num_bloqueados--;
                    i--;
                    printf("[DESBLOQUEO] %s listo.\n", jugador->nombre);
                }
            }
        }
        
        pthread_mutex_unlock(&mutex);
        nanosleep(&sleep_time, NULL); // Esperar 100ms
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

void mover_a_cola_bloqueados(int id_jugador)
{
    // Bloquear el jugador en su PCB
    pcbs[id_jugador - 1].estado = BLOQUEADO;
    pcbs[id_jugador - 1].tiempo_bloqueado = rand() % (20 - 10 + 1) + 10; // Tiempo aleatorio entre 10 y 20

    // Agregar a la cola de bloqueados
    if (num_bloqueados < NUM_JUGADORES)
    {
        cola_bloqueados[num_bloqueados++] = id_jugador;
    }

    printf("Jugador %d bloqueado por %d segundos\n", id_jugador, pcbs[id_jugador - 1].tiempo_bloqueado);
}

void verificar_cola_bloqueados()
{
    for (int i = 0; i < num_bloqueados; i++)
    {
        pcbs[cola_bloqueados[i] - 1].tiempo_restante--;
        if (pcbs[cola_bloqueados[i] - 1].tiempo_restante <= 0)
        {
            printf("Jugador %d ha terminado su tiempo en E/S. Moviendo a la cola de listos.\n", cola_bloqueados[i]);
            agregar_a_cola_listos(cola_bloqueados[i]);
            pcbs[cola_bloqueados[i] - 1].estado = 1; // Listo

            // Eliminar jugador de la cola de bloqueados
            for (int j = i; j < num_bloqueados - 1; j++)
            {
                cola_bloqueados[j] = cola_bloqueados[j + 1];
            }
            num_bloqueados--;
            i--; // Ajustar índice después de eliminar
        }
    }
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
        printf("Jugador %d (%s): %d cartas, %d puntos\n",
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

        printf("Turno del jugador: %s (ID: %d)\n", jugador_actual->nombre, jugador_id);

        // Verificar si el jugador tiene tiempo restante
        if (jugador_actual->tiempo_restante <= 0)
        {
            printf("Jugador %s ha perdido su turno por falta de tiempo.\n", jugador_actual->nombre);
            pthread_mutex_unlock(&mutex);
            continue; // Pasar al siguiente jugador
        }

        // Simulación del turno
        int tiempo_juego = (modo == 'R' && jugador_actual->tiempo_restante > QUANTUM) ? QUANTUM : jugador_actual->tiempo_restante;
        sleep(tiempo_juego);
        jugador_actual->tiempo_restante -= tiempo_juego;

        if (jugador_actual->tiempo_restante <= 0)
        {
            printf("Tiempo agotado para el jugador %s\n", jugador_actual->nombre);
        }

        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
    return NULL;
}

void mostrar_politica_actual()
{
    printf("\nPolítica actual: %s\n",
           (modo == 'F') ? "FCFS (Turnos completos en orden de llegada)" : "Round Robin (Quantum de 20 segundos)");
    printf("───────────────────────────────────────────────────────\n");
}

void elegir_politica()
{
    printf("\n╔════════════════════════════════════════════╗");
    printf("\n║   SELECCIÓN DE POLÍTICA DE PLANIFICACIÓN   ║");
    printf("\n╠════════════════════════════════════════════╣");
    printf("\n║ F - First Come First Served (FCFS)         ║");
    printf("\n║ R - Round Robin (Quantum 20 segundos)      ║");
    printf("\n╚════════════════════════════════════════════╝");
    printf("\nSeleccione política (F/R): ");

    char opcion;
    scanf(" %c", &opcion);
    getchar(); // Limpiar buffer

    if (opcion == 'F' || opcion == 'f')
    {
        modo = 'F';
        printf("\n✔ Política cambiada a FCFS: Turnos completos en orden de llegada\n");
    }
    else if (opcion == 'R' || opcion == 'r')
    {
        modo = 'R';
        printf("\n✔ Política cambiada a Round Robin: Quantum de %d segundos\n", QUANTUM);
    }
    else
    {
        printf("\n➜ Manteniendo política actual: %s\n",
               (modo == 'F') ? "FCFS" : "Round Robin");
    }

    mostrar_politica_actual();
}

// Función auxiliar para mostrar mensajes de robo de carta
void mostrar_robo_carta(const carta_t *carta, bool es_automatico)
{
    printf("%s: %d de %s\n",
           es_automatico ? "Robaste automáticamente" : "Robaste",
           carta->numero,
           carta->color);
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
    inicializar_mapa_bits(&manejador);
    inicializar_memoria(&gm, TAMANO_MEMORIA_LISTA);
    inicializar_memoria_virtual(); // Inicializar memoria virtual


    // 4. Configurar nombres de jugadores
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        char nombre[MAX_NOMBRE];
        printf("Ingrese nombre para Jugador %d: ", i + 1);

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
    if (pthread_create(&hilo_es, NULL, verificar_bloqueados, &control) != 0 ||
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