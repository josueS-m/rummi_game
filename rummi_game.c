#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <limits.h>

// ----------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------
#define NUM_JUGADORES 4          // Número fijo de jugadores
#define MAX_CARTAS 108           // Total cartas en el mazo (standard para Rummy)
#define QUANTUM 5                // Tiempo por turno en segundos
#define PUNTOS_MINIMOS_APEADA 30 // Mínimo para primera apeada

#define CARTAS_INICIALES 14 // Cartas al repartir (puede variar según reglas)
#define MAX_COLOR 15        // Longitud máxima para nombre de color
#define MAX_NOMBRE 50       // Longitud máxima para nombre jugador

#define MAX_CARTAS_GRUPO 4     // Máximo cartas en grupo (ej: 4 sietes)
#define MAX_CARTAS_ESCALERA 13 // Máximo en escalera (A-2-...-K)
#define MAX_GRUPOS 10          // Máximo grupos en mesa
#define MAX_ESCALERAS 10       // Máximo escaleras en mesa
#define VALOR_COMODIN 0  // Valor numérico para comodines
#define MIN_CARTAS_GRUPO 3  // Mínimo cartas para un grupo
#define MIN_CARTAS_ESCALERA 3 // Mínimo cartas para escalera


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

typedef struct
{
    mano_t mano;             // Cartas en mano del jugador
    char nombre[MAX_NOMBRE]; // Nombre del jugador
    int id;                  // Identificador único
    int tiempo_restante;     // Tiempo disponible (para turnos)
    int puntos_suficientes;  // Flag si alcanzó puntos mínimos
    bool en_juego;           // Estado activo/inactivo
    bool carta_agregada;     // Nueva bandera: indica si ya agregó una carta en el turno
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
    int id_jugador;             // ID del jugador
    char nombre[20];            // Nombre abreviado
    int cartas_en_mano;         // Cartas actuales
    int puntos;                 // Puntos acumulados
    int partidas_jugadas;       // Total partidas
    int partidas_ganadas;       // Victorias
    int partidas_perdidas;      // Derrotas
    int estado;                 // Estado actual
    int tiempo_total_juego;     // Tiempo jugado acumulado
    int turnos_jugados;         // Turnos tomados
    int cartas_robadas;         // Cartas robadas
    int cartas_descartadas;     // Cartas descartadas
    int grupos_formados;        // Grupos creados
    int escaleras_formadas;     // Escaleras creadas
    int victorias_con_escalera; // Victorias con escalera completa
    int tiempo_restante;        // Tiempo en turno actual
} pcb_t;


// Estados posibles de un jugador
typedef enum {
    BLOQUEADO,
    LISTO, 
    EJECUTANDO
} estado_jugador;

// Estructura para gestionar estado de procesos (jugadores)
typedef struct {
    int id_jugador;          // ID del jugador
    estado_jugador estado;    // Estado actual
    int tiempo_bloqueado;    // Tiempo restante de bloqueo
} estado_proceso;

// ----------------------------------------------------------------------
// Variables Globales
// ----------------------------------------------------------------------
jugador_t jugadores[NUM_JUGADORES];                     // Array de jugadores
pcb_t pcbs[NUM_JUGADORES];                              // Array de PCBs (estadísticas)
banco_de_apeadas_t banco_apeadas;                       // Banco de combinaciones (grupos/escaleras)
int turno_actual = 0;                                   // Turno actual (índice del jugador)
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;      // Mutex general
pthread_mutex_t mutex_mesa = PTHREAD_MUTEX_INITIALIZER; // Mutex para el banco

// Variables para el scheduler (planificador)
char modo = 'F';                    // Modo de scheduling: 'F' (FCFS) o 'R' (Round Robin)
int cola_listos[NUM_JUGADORES];     // Cola de jugadores listos para jugar
int frente = 0, final = 0;          // Índices para la cola circular
int cola_bloqueados[NUM_JUGADORES]; // Cola de jugadores bloqueados
int num_bloqueados = 0;             // Contador de jugadores bloqueados
pthread_t hilo_juego;            // Hilo del juego
pthread_cond_t cond_turno = PTHREAD_COND_INITIALIZER; // Variable de condición para el juego
int proceso_en_ejecucion = -1; // ID del proceso en ejecución
int tiempo_restante_quantum = 0; // Tiempo restante del proceso en ejecución

estado_proceso estados_jugadores[NUM_JUGADORES];  // Array de estados de jugadores
mazo_t mazo; // Mazo de cartas
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
//void *jugador_thread(void *arg);

void mano_inicializar(mano_t *mano, int capacidad) {
    if (mano == NULL || capacidad <= 0) {
        fprintf(stderr, "Error: Parámetros inválidos para inicializar mano\n");
        exit(EXIT_FAILURE);
    }
    
    mano->cartas = (carta_t*)malloc(sizeof(carta_t) * capacidad);
    if (mano->cartas == NULL) {
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

    // Verificar suficientes cartas para todos los jugadores
    if (mazo->cantidad < CARTAS_INICIALES * NUM_JUGADORES) {
        fprintf(stderr, "Error: No hay suficientes cartas en el mazo (%d necesarias, %d disponibles)\n", 
               CARTAS_INICIALES * NUM_JUGADORES, mazo->cantidad);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_JUGADORES; i++) {
        // Inicializar datos básicos del jugador
        jugadores[i].id = i + 1;  // IDs comienzan en 1
        snprintf(jugadores[i].nombre, MAX_NOMBRE, "Jugador %d", i + 1);
        
        // Inicializar mano
        mano_inicializar(&jugadores[i].mano, CARTAS_INICIALES);
        
        // Repartir cartas (tomando del final del mazo)
        for (int j = 0; j < CARTAS_INICIALES; j++) {
            jugadores[i].mano.cartas[j] = mazo->cartas[mazo->cantidad - 1];
            mazo->cantidad--;
            jugadores[i].mano.cantidad++;
        }
    }
}

void banco_inicializar(banco_de_apeadas_t *banco) {
    if (banco == NULL) {
        fprintf(stderr, "Error: Puntero a banco inválido\n");
        exit(EXIT_FAILURE);
    }

    banco->grupos = (grupo_t*)malloc(sizeof(grupo_t) * MAX_GRUPOS);
    if (banco->grupos == NULL) {
        fprintf(stderr, "Error al asignar memoria para los grupos del banco\n");
        exit(EXIT_FAILURE);
    }

    banco->escaleras = (escalera_t*)malloc(sizeof(escalera_t) * MAX_ESCALERAS);
    if (banco->escaleras == NULL) {
        fprintf(stderr, "Error al asignar memoria para las escaleras del banco\n");
        free(banco->grupos);
        exit(EXIT_FAILURE);
    }

    banco->total_grupos = 0;
    banco->total_escaleras = 0;

    // Inicializar cada grupo y escalera en el banco
    for (int i = 0; i < MAX_GRUPOS; i++) {
        banco->grupos[i].cantidad = 0;
        memset(banco->grupos[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_GRUPO);
    }
    
    for (int i = 0; i < MAX_ESCALERAS; i++) {
        banco->escaleras[i].cantidad = 0;
        memset(banco->escaleras[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_ESCALERA);
    }
}

void apeada_inicializar(apeada_t *apeada) {
    if (apeada == NULL) {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        exit(EXIT_FAILURE);
    }

    apeada->grupos = (grupo_t*)malloc(sizeof(grupo_t) * MAX_GRUPOS);
    if (apeada->grupos == NULL) {
        fprintf(stderr, "Error al asignar memoria para los grupos de apeada\n");
        exit(EXIT_FAILURE);
    }

    apeada->escaleras = (escalera_t*)malloc(sizeof(escalera_t) * MAX_ESCALERAS);
    if (apeada->escaleras == NULL) {
        fprintf(stderr, "Error al asignar memoria para las escaleras de apeada\n");
        free(apeada->grupos);
        exit(EXIT_FAILURE);
    }

    apeada->total_grupos = 0;
    apeada->total_escaleras = 0;

    // Inicializar cada grupo y escalera en la apeada
    for (int i = 0; i < MAX_GRUPOS; i++) {
        apeada->grupos[i].cantidad = 0;
        memset(apeada->grupos[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_GRUPO);
    }
    
    for (int i = 0; i < MAX_ESCALERAS; i++) {
        apeada->escaleras[i].cantidad = 0;
        memset(apeada->escaleras[i].cartas, 0, sizeof(carta_t) * MAX_CARTAS_ESCALERA);
    }
}

void mano_liberar(mano_t *mano) {
    if (mano != NULL) {
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

void banco_liberar(banco_de_apeadas_t *banco) {
    if (banco == NULL) return;

    if (banco->grupos != NULL) {
        free(banco->grupos);
        banco->grupos = NULL;
    }

    if (banco->escaleras != NULL) {
        free(banco->escaleras);
        banco->escaleras = NULL;
    }

    banco->total_grupos = 0;
    banco->total_escaleras = 0;
}

void apeada_liberar(apeada_t *apeada) {
    if (apeada == NULL) return;

    if (apeada->grupos != NULL) {
        free(apeada->grupos);
        apeada->grupos = NULL;
    }

    if (apeada->escaleras != NULL) {
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
int calcular_puntos_apeada(const apeada_t *apeada) {
    // Validación básica
    if (apeada == NULL) {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        return 0;
    }

    int puntos_totales = 0;

    // Sumar puntos de todos los grupos
    for (int i = 0; i < apeada->total_grupos; i++) {
        puntos_totales += calcular_puntos_grupo(apeada->grupos[i].cartas, apeada->grupos[i].cantidad);
    }

    // Sumar puntos de todas las escaleras
    for (int i = 0; i < apeada->total_escaleras; i++) {
        puntos_totales += calcular_puntos_escalera(apeada->escaleras[i].cartas, apeada->escaleras[i].cantidad);
    }

    return puntos_totales;
}

// ----------------------------------------------------------------------
// Funciones de logica de apeada
// ----------------------------------------------------------------------
 
// Verifica si un conjunto de cartas forma un grupo válido
bool es_grupo_valido(const carta_t cartas[], int cantidad) {
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_CARTAS_GRUPO) {
        return false;
    }

    int numero_base = -1;
    int comodines = 0;
    char colores_usados[MAX_CARTAS_GRUPO][MAX_COLOR] = {0};
    int colores_count = 0;

    for (int i = 0; i < cantidad; i++) {
        if (cartas[i].numero == VALOR_COMODIN) { // Usando VALOR_COMODIN
            comodines++;
        } else {
            // Verificar que no haya cartas duplicadas (mismo número y color)
            for (int j = 0; j < colores_count; j++) {
                if (strcmp(cartas[i].color, colores_usados[j]) == 0) {
                    return false; // Color repetido en el grupo
                }
            }

            // Guardar color usado
            strncpy(colores_usados[colores_count++], cartas[i].color, MAX_COLOR - 1);

            // Establecer o verificar número base
            if (numero_base == -1) {
                numero_base = cartas[i].numero;
            } else if (cartas[i].numero != numero_base) {
                return false; // Números diferentes
            }
        }
    }

    // Debe haber al menos una carta no comodín
    return (numero_base != -1);
}
 
// Verifica si un conjunto de cartas forma una escalera válida
bool es_escalera_valida(const carta_t cartas[], int cantidad) {
    // Verificar límites de cantidad
    if (cantidad < 3 || cantidad > MAX_CARTAS_ESCALERA) {
        return false;
    }

    int numeros[MAX_CARTAS_ESCALERA] = {0};
    char color_base[MAX_COLOR] = "";
    int idx = 0;
    int comodines = 0;

    // Procesar cartas y verificar color consistente
    for (int i = 0; i < cantidad; i++) {
        if (cartas[i].numero == VALOR_COMODIN) {
            comodines++;
        } else {
            // Verificar color consistente
            if (strlen(color_base) == 0) {
                strncpy(color_base, cartas[i].color, MAX_COLOR - 1);
            } else if (strcmp(color_base, cartas[i].color) != 0) {
                return false; // Diferente color
            }

            // Verificar que no haya números duplicados
            for (int j = 0; j < idx; j++) {
                if (numeros[j] == cartas[i].numero) {
                    return false; // Número duplicado
                }
            }

            numeros[idx++] = cartas[i].numero;
        }
    }

    // No puede ser solo comodines
    if (idx == 0) return false;

    // Ordenar números (usando bubble sort por simplicidad)
    for (int i = 0; i < idx - 1; i++) {
        for (int j = i + 1; j < idx; j++) {
            if (numeros[i] > numeros[j]) {
                int temp = numeros[i];
                numeros[i] = numeros[j];
                numeros[j] = temp;
            }
        }
    }

    // Verificar secuencia con comodines
    int comodines_necesarios = 0;
    for (int i = 0; i < idx - 1; i++) {
        int diff = numeros[i + 1] - numeros[i];
        if (diff <= 0) {
            return false; // Números iguales o decrecientes
        }
        comodines_necesarios += (diff - 1);
    }

    // Comodines deben poder llenar todos los huecos
    if (comodines_necesarios > comodines) {
        return false;
    }

    // Comodines restantes solo pueden estar al inicio o final
    int comodines_restantes = comodines - comodines_necesarios;
    if (comodines_restantes > 0) {
        bool comodin_inicio = false;
        bool comodin_final = false;
        
        for (int i = 0; i < cantidad; i++) {
            if (cartas[i].numero == VALOR_COMODIN) {
                if (i > 0 && i < cantidad - 1) {
                    // Comodín en medio debe estar rellenando un hueco
                    if (comodines_restantes > 0) {
                        return false;
                    }
                } else {
                    if (i == 0) comodin_inicio = true;
                    if (i == cantidad - 1) comodin_final = true;
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
void mostrar_apeada(const apeada_t *apeada) {
    // Validación de parámetros
    if (apeada == NULL) {
        fprintf(stderr, "Error: Puntero a apeada inválido\n");
        return;
    }

    // Caso especial: apeada vacía
    if (apeada->total_grupos == 0 && apeada->total_escaleras == 0) {
        printf("\n=== No se realizaron combinaciones válidas ===\n");
        return;
    }

    // Encabezado informativo
    printf("\n=== Combinaciones de Apeada ===\n");

    // Mostrar grupos
    if (apeada->total_grupos > 0) {
        printf("\n── Grupos ──\n");
        for (int i = 0; i < apeada->total_grupos; i++) {
            printf("Grupo %d: ", i + 1);
            for (int j = 0; j < apeada->grupos[i].cantidad; j++) {
                if (apeada->grupos[i].cartas[j].numero == 0) {
                    printf("[Comodín] ");
                } else {
                    printf("[%d %s] ", apeada->grupos[i].cartas[j].numero, 
                                      apeada->grupos[i].cartas[j].color);
                }
            }
            printf("\n");
        }
    }

    // Mostrar escaleras
    if (apeada->total_escaleras > 0) {
        printf("\n── Escaleras ──\n");
        for (int i = 0; i < apeada->total_escaleras; i++) {
            printf("Escalera %d: ", i + 1);
            for (int j = 0; j < apeada->escaleras[i].cantidad; j++) {
                if (apeada->escaleras[i].cartas[j].numero == 0) {
                    printf("[Comodín] ");
                } else {
                    printf("[%d %s] ", apeada->escaleras[i].cartas[j].numero, 
                                      apeada->escaleras[i].cartas[j].color);
                }
            }
            printf("\n");
        }
    }

    // Mostrar resumen
    if (apeada->total_grupos > 0 || apeada->total_escaleras > 0) {
        printf("\nResumen:\n");
        printf("- Total grupos: %d\n", apeada->total_grupos);
        printf("- Total escaleras: %d\n", apeada->total_escaleras);
    }

    printf("────────────────────────────\n");
}
 
apeada_t crear_mejor_apeada(jugador_t *jugador) {
    apeada_t mejor_apeada;
    apeada_inicializar(&mejor_apeada);
    int max_puntos = 0;
    bool apeada_valida = false;
    mano_t mejor_mano; // Guardaremos la mejor mano temporal aquí

    // Inicializar mano temporal para backup
    mano_inicializar(&mejor_mano, jugador->mano.capacidad);
    memcpy(mejor_mano.cartas, jugador->mano.cartas, jugador->mano.cantidad * sizeof(carta_t));
    mejor_mano.cantidad = jugador->mano.cantidad;

    // Probar diferentes estrategias
    for (int estrategia = 0; estrategia < 3; estrategia++) {
        mano_t mano_temp;
        mano_inicializar(&mano_temp, jugador->mano.capacidad);
        memcpy(mano_temp.cartas, jugador->mano.cartas, jugador->mano.cantidad * sizeof(carta_t));
        mano_temp.cantidad = jugador->mano.cantidad;

        apeada_t apeada_temp;
        apeada_inicializar(&apeada_temp);
        int puntos_temp = 0;

        // Aplicar estrategia
        switch (estrategia) {
            case 0: buscar_combinaciones(&mano_temp, &apeada_temp, &puntos_temp, true); break;
            case 1: buscar_combinaciones(&mano_temp, &apeada_temp, &puntos_temp, false); break;
            case 2: buscar_combinacion_mixta(&mano_temp, &apeada_temp, &puntos_temp); break;
        }

        // Validar apeada antes de considerar como mejor
        bool cumple_minimo = jugador->puntos_suficientes || puntos_temp >= PUNTOS_MINIMOS_APEADA;
        
        if (puntos_temp > max_puntos && cumple_minimo) {
            max_puntos = puntos_temp;
            apeada_liberar(&mejor_apeada);
            mejor_apeada = apeada_temp;
            
            // Guardar estado temporal de la mano (pero no aplicar aún)
            memcpy(mejor_mano.cartas, mano_temp.cartas, mano_temp.cantidad * sizeof(carta_t));
            mejor_mano.cantidad = mano_temp.cantidad;
            apeada_valida = true;
        } else {
            apeada_liberar(&apeada_temp);
        }

        mano_liberar(&mano_temp);
    }

    // Solo aplicar cambios a la mano real si la apeada es válida
    if (apeada_valida) {
        memcpy(jugador->mano.cartas, mejor_mano.cartas, mejor_mano.cantidad * sizeof(carta_t));
        jugador->mano.cantidad = mejor_mano.cantidad;
    }

    mano_liberar(&mejor_mano);
    return mejor_apeada;
}
 


bool realizar_apeada_optima(jugador_t *jugador, banco_de_apeadas_t *banco_mesa) {
    if (!jugador->en_juego || jugador->mano.cantidad < 3) {
        printf("%s no puede realizar apeada (no está en juego o tiene muy pocas cartas).\n", 
               jugador->nombre);
        return false;
    }

    printf("22222222222222222222\n");
    apeada_t apeada_jugador = crear_mejor_apeada(jugador);
    int puntos_apeada = calcular_puntos_apeada(&apeada_jugador);



    // Validación estricta para primera apeada
    if (!jugador->puntos_suficientes) {
        if (puntos_apeada >= PUNTOS_MINIMOS_APEADA) {
            jugador->puntos_suficientes = true;
            printf("%s ha realizado su primera apeada con %d puntos (mínimo requerido: %d)!\n",
                   jugador->nombre, puntos_apeada, PUNTOS_MINIMOS_APEADA);
        } else {
            printf("%s no alcanzó el mínimo de %d puntos para la primera apeada (obtuvo %d).\n",
                   jugador->nombre, PUNTOS_MINIMOS_APEADA, puntos_apeada);
            apeada_liberar(&apeada_jugador);
            return false;
        }
    } else if (puntos_apeada == 0) {
        printf("%s no tiene combinaciones válidas para apear en este turno.\n", jugador->nombre);
        apeada_liberar(&apeada_jugador);
        return false;
    }

    // Mostrar detalles de la apeada
    mostrar_apeada(&apeada_jugador);
    printf("5555555555555\n");
    // Bloquear acceso concurrente al banco
    //pthread_mutex_lock(&mutex_mesa); por alguna razon revienta 
    printf("5555555555555\n");
    
    // Transferir grupos al banco
    for (int i = 0; i < apeada_jugador.total_grupos && banco_mesa->total_grupos < MAX_GRUPOS; i++) {
        banco_mesa->grupos[banco_mesa->total_grupos++] = apeada_jugador.grupos[i];
    }
    
    // Transferir escaleras al banco
    for (int i = 0; i < apeada_jugador.total_escaleras && banco_mesa->total_escaleras < MAX_ESCALERAS; i++) {
        banco_mesa->escaleras[banco_mesa->total_escaleras++] = apeada_jugador.escaleras[i];
    }
    
    pthread_mutex_unlock(&mutex_mesa);

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
bool intentar_embonar_carta(carta_t *carta, banco_de_apeadas_t *banco) {
    // Intentar embonar en grupos
    for (int i = 0; i < banco->total_grupos; i++) {
        if (puede_embonar_grupo(carta, &banco->grupos[i])) {
            banco->grupos[i].cartas[banco->grupos[i].cantidad++] = *carta;
            return true;
        }
    }

    // Intentar embonar en escaleras
    for (int i = 0; i < banco->total_escaleras; i++) {
        if (puede_embonar_escalera(carta, &banco->escaleras[i])) {
            banco->escaleras[i].cartas[banco->escaleras[i].cantidad++] = *carta;
            return true;
        }
    }

    // Si no se pudo embonar, intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(carta, 1)) {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->cartas[0] = *carta;
        nuevo_grupo->cantidad = 1;
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(carta, 1)) {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->cartas[0] = *carta;
        nueva_escalera->cantidad = 1;
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Función para intentar mover comodines y luego embonar la carta
bool intentar_mover_comodin_y_embonar(carta_t *carta, banco_de_apeadas_t *banco) {
    if (mover_comodin_para_embonar(banco, carta)) {
        return intentar_embonar_carta(carta, banco);
    }
    return false;
}

// Función principal para embonar una carta del jugador al banco
bool embonar_carta(jugador_t *jugador, banco_de_apeadas_t *banco, int indice_carta) {
    if (indice_carta < 0 || indice_carta >= jugador->mano.cantidad) {
        return false; // Índice inválido
    }

    carta_t carta = jugador->mano.cartas[indice_carta];

    // Intentar embonar directamente
    if (intentar_embonar_carta(&carta, banco)) {
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    // Intentar mover comodines y luego embonar
    if (intentar_mover_comodin_y_embonar(&carta, banco)) {
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    // Intentar crear un nuevo grupo o escalera
    if (banco->total_grupos < MAX_GRUPOS && es_grupo_valido(&carta, 1)) {
        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
        nuevo_grupo->cartas[0] = carta;
        nuevo_grupo->cantidad = 1;
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    if (banco->total_escaleras < MAX_ESCALERAS && es_escalera_valida(&carta, 1)) {
        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
        nueva_escalera->cartas[0] = carta;
        nueva_escalera->cantidad = 1;
        remover_carta(&jugador->mano, indice_carta);
        return true;
    }

    return false; // No se pudo embonar ni crear un nuevo grupo/escalera
}

// Función para que el jugador intente embonar todas las cartas posibles
void jugador_embonar_cartas(jugador_t *jugador, banco_de_apeadas_t *banco) {
    bool embonada_algo = true;

    while (embonada_algo) {
        embonada_algo = false;

        // Intentar embonar cada carta (de atrás hacia adelante para evitar problemas de índices)
        for (int i = jugador->mano.cantidad - 1; i >= 0; i--) {
            if (embonar_carta(jugador, banco, i)) {
                embonada_algo = true;
                break; // Reiniciar el bucle porque los índices han cambiado
            }
        }
    }
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

// ----------------------------------------------------------------------
// Funciones para la Mano
// ----------------------------------------------------------------------
void agregar_carta(mano_t *mano, carta_t carta) {
    if (mano->cantidad < mano->capacidad) {
        mano->cartas[mano->cantidad] = carta;
        mano->cantidad++;
    } else {
        // Si la mano está llena, aumentar la capacidad y reasignar memoria
        mano->capacidad *= 2;
        mano->cartas = realloc(mano->cartas, sizeof(carta_t) * mano->capacidad);
        if (mano->cartas == NULL) {
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
// Funciones de Inicialización de Jugadores y PCB
// ----------------------------------------------------------------------
void inicializar_jugador(jugador_t *jugador, int id, char nombre[], carta_t cartas[], int cantidad)
{
    jugador->id = id;
    strcpy(jugador->nombre, nombre);
    jugador->mano.cantidad = 0;
    // Suponemos que "cantidad" es el número de cartas iniciales
    for (int i = 0; i < cantidad; i++)
    {
        agregar_carta(&(jugador->mano), cartas[i]);
    }
}

void actualizar_pcb(pcb_t *pcb, jugador_t *jugador)
{
    pcb->id_jugador = jugador->id;
    strcpy(pcb->nombre, jugador->nombre);
    pcb->cartas_en_mano = jugador->mano.cantidad;
    pcb->puntos = 0;
    pcb->partidas_jugadas = 0;
    pcb->partidas_ganadas = 0;
    pcb->partidas_perdidas = 0;
    pcb->estado = 1;
    pcb->tiempo_total_juego = 0;
    pcb->turnos_jugados = 0;
    pcb->cartas_robadas = 0;
    pcb->cartas_descartadas = 0;
    pcb->grupos_formados = 0;
    pcb->escaleras_formadas = 0;
    pcb->victorias_con_escalera = 0;
    pcb->tiempo_restante = 0;
}

void escribir_pcb(pcb_t jugador)
{
    char filename[30];
    sprintf(filename, "PCB_Jugador%d.txt", jugador.id_jugador);

    FILE *file = fopen(filename, "w");
    if (file == NULL)
    {
        printf("Error al abrir %s\n", filename);
        return;
    }

    fprintf(file, "ID: %d\n", jugador.id_jugador);
    fprintf(file, "Nombre: %s\n", jugador.nombre);
    fprintf(file, "Cartas en Mano: %d\n", jugador.cartas_en_mano);
    fprintf(file, "Puntos: %d\n", jugador.puntos);
    fprintf(file, "Partidas Jugadas: %d\n", jugador.partidas_jugadas);
    fprintf(file, "Partidas Ganadas: %d\n", jugador.partidas_ganadas);
    fprintf(file, "Partidas Perdidas: %d\n", jugador.partidas_perdidas);
    fprintf(file, "Estado: %d\n", jugador.estado);
    fprintf(file, "Tiempo Total de Juego: %d\n", jugador.tiempo_total_juego);
    fprintf(file, "Turnos Jugados: %d\n", jugador.turnos_jugados);
    fprintf(file, "Cartas Robadas: %d\n", jugador.cartas_robadas);
    fprintf(file, "Cartas Descartadas: %d\n", jugador.cartas_descartadas);
    fprintf(file, "Grupos Formados: %d\n", jugador.grupos_formados);
    fprintf(file, "Escaleras Formadas: %d\n", jugador.escaleras_formadas);
    fprintf(file, "Victorias con Escalera: %d\n", jugador.victorias_con_escalera);

    fclose(file);
}

void actualizar_tabla_procesos(pcb_t jugadores[], int num_jugadores)
{
    FILE *file = fopen("tabla_procesos.txt", "w");
    if (file == NULL)
    {
        printf("Error al abrir tabla_procesos.txt\n");
        return;
    }

    fprintf(file, "ID\tNombre\tCartas\tPuntos\tEstado\n");
    for (int i = 0; i < num_jugadores; i++)
    {
        fprintf(file, "%d\t%s\t%d\t%d\t%d\n",
                jugadores[i].id_jugador, jugadores[i].nombre,
                jugadores[i].cartas_en_mano, jugadores[i].puntos,
                jugadores[i].estado);
    }
    fclose(file);
}

// ----------------------------------------------------------------------
// Función para repartir cartas entre jugadores
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

// ----------------------------------------------------------------------
// Funciones de Concurrencia y Manejo de Turnos
// ----------------------------------------------------------------------

// Hilo principal que controla el estado global del juego
void* hilo_juego_func(void* arg) {
    while(1) {
        pthread_mutex_lock(&mutex);
        
        // Detectar ganador continuamente
        int ganador = determinar_ganador(jugadores, NUM_JUGADORES, mazo.cantidad == 0);
        if(ganador != -1) {
            printf("\n¡Jugador %d (%s) ha ganado!\n", jugadores[ganador].id, jugadores[ganador].nombre);
            
            // Actualizar estadísticas de todos los jugadores
            for(int i = 0; i < NUM_JUGADORES; i++) {
                pcbs[i].partidas_jugadas++;
                if(i == ganador) pcbs[i].partidas_ganadas++;
                else pcbs[i].partidas_perdidas++;
                escribir_pcb(pcbs[i]);
            }
            
            actualizar_tabla_procesos(pcbs, NUM_JUGADORES);
            exit(0);
        }
        
        // Cambiar política de planificación con teclas F/R
        if(kbhit()) {
            char c = getchar();
            if(c == 'F' || c == 'f') modo = 'F';
            else if(c == 'R' || c == 'r') modo = 'R';
            printf("\nPolítica cambiada a %c\n", modo);
        }
        
        pthread_mutex_unlock(&mutex);
        sleep(1);  // Revisar estado cada segundo
    }
    return NULL;
}

// Hilo que gestiona la asignación de turnos y políticas
void* planificador(void* arg) {
    while(1) {
        pthread_mutex_lock(&mutex);
        
        // Actualizar tiempo de bloqueo de jugadores
        for(int i = 0; i < NUM_JUGADORES; i++) {
            if(estados_jugadores[i].estado == BLOQUEADO) {
                if(--estados_jugadores[i].tiempo_bloqueado <= 0) {
                    estados_jugadores[i].estado = LISTO;
                    agregar_a_cola_listos(estados_jugadores[i].id_jugador);
                }
            }
        }
        
        // Asignar nuevo turno si no hay nadie ejecutando
        if(proceso_en_ejecucion == -1) {
            int siguiente = siguiente_turno();
            if(siguiente != -1) {
                proceso_en_ejecucion = siguiente;
                estados_jugadores[siguiente-1].estado = EJECUTANDO;
                tiempo_restante_quantum = QUANTUM;
                pthread_cond_signal(&cond_turno);  // Notificar al jugador
            }
        }
        
        // Manejar quantum en Round Robin
        if(modo == 'R' && proceso_en_ejecucion != -1) {
            if(--tiempo_restante_quantum <= 0) {
                estados_jugadores[proceso_en_ejecucion-1].estado = LISTO;
                agregar_a_cola_listos(proceso_en_ejecucion);
                proceso_en_ejecucion = -1;
            }
        }
        
        pthread_mutex_unlock(&mutex);
        sleep(1);  // Intervalo de planificación
    }
}

// ----------------------------------------------------------------------
// Función del hilo del jugador
// ----------------------------------------------------------------------

// Función que ejecuta cada hilo de jugador
void* jugador_thread(void* arg) {
    jugador_t* jugador = (jugador_t*)arg;
    pcb_t* pcb = &pcbs[jugador->id - 1];
    
    while(1) {
        pthread_mutex_lock(&mutex);
        
        // Esperar hasta que sea su turno
        while(proceso_en_ejecucion != jugador->id || !jugador->en_juego) {
            pthread_cond_wait(&cond_turno, &mutex);
        }
        
        // --- INICIO DEL TURNO ---
        printf("\n=== TURNO JUGADOR %d (%s) ===\n", jugador->id, jugador->nombre);
        mostrar_mano(&jugador->mano);
        
        // Menú de acciones
        int opcion;
        printf("\n1. Robar carta\n2. Hacer apeada\n3. Embonar carta\n4. Descartar carta\n> ");
        scanf("%d", &opcion);
        
        // Procesar acción seleccionada
        switch(opcion) {
            case 1:  // Robar carta
                if(mazo.cantidad > 0) {
                    carta_t nueva = mazo.cartas[--mazo.cantidad];
                    agregar_carta(&jugador->mano, nueva);
                    pcb->cartas_robadas++;
                    printf("Robaste: %d de %s\n", nueva.numero, nueva.color);
                } else {
                    printf("¡No hay cartas en el mazo!\n");
                    // Bloquear jugador si no puede robar
                    estados_jugadores[jugador->id-1].estado = BLOQUEADO;
                    estados_jugadores[jugador->id-1].tiempo_bloqueado = rand() % 5 + 1;
                    printf("Jugador %d bloqueado por %d segundos\n", 
                           jugador->id, estados_jugadores[jugador->id-1].tiempo_bloqueado);
                }
                break;
                
            case 2:  // Hacer apeada
                if(jugador->mano.cantidad > 0) {
                    realizar_apeada_optima(jugador, &banco_apeadas);
                } else {
                    printf("No tienes cartas para hacer apeada\n");
                }
                break;
                
            case 3:  // Embonar carta
                if(banco_apeadas.total_grupos > 0 || banco_apeadas.total_escaleras > 0) {
                    printf("Seleccione carta para embonar (1-%d): ", jugador->mano.cantidad);
                    int pos;
                    scanf("%d", &pos);
                    if(pos >= 1 && pos <= jugador->mano.cantidad) {
                        if(embonar_carta(jugador, &banco_apeadas, pos-1)) {
                            printf("¡Carta embonada con éxito!\n");
                        } else {
                            printf("No se pudo embonar la carta\n");
                        }
                    } else {
                        printf("Posición inválida\n");
                    }
                } else {
                    printf("No hay combinaciones en la mesa para embonar\n");
                }
                break;
                
            case 4:  // Descartar carta
                if(jugador->mano.cantidad > 0) {
                    printf("Seleccione carta a descartar (1-%d): ", jugador->mano.cantidad);
                    int pos;
                    scanf("%d", &pos);
                    if(pos >= 1 && pos <= jugador->mano.cantidad) {
                        remover_carta(&jugador->mano, pos-1);
                        pcb->cartas_descartadas++;
                        printf("Carta descartada\n");
                    } else {
                        printf("Posición inválida\n");
                    }
                } else {
                    printf("No tienes cartas para descartar\n");
                }
                break;
                
            default:
                printf("Opción inválida\n");
        }
        
        // --- FINALIZACIÓN DEL TURNO ---
        // Actualizar PCB
        pcb->cartas_en_mano = jugador->mano.cantidad;
        pcb->turnos_jugados++;
        
        // Manejar finalización según política
        if(modo == 'F') {  // FCFS: termina turno completo
            estados_jugadores[jugador->id-1].estado = LISTO;
            agregar_a_cola_listos(jugador->id);
            proceso_en_ejecucion = -1;
        } 
        // En Round Robin, el planificador maneja el quantum
        
        // Verificar si el jugador ganó
        if(jugador->mano.cantidad == 0) {
            printf("¡Jugador %d se ha quedado sin cartas!\n", jugador->id);
            jugador->en_juego = false;
            estados_jugadores[jugador->id-1].estado = BLOQUEADO;  // Ya no participa
        }
        
        pthread_mutex_unlock(&mutex);
        usleep(100000);  // Pequeña pausa para evitar congestión
    }
    return NULL;
}

void agregar_a_cola_listos(int id_jugador)
{
    if (num_listos == NUM_JUGADORES)
    {
        printf("Cola llena. No se puede agregar al jugador %d\n", id_jugador);
        return;
    }
    cola_listos[final] = id_jugador;
    final = (final + 1) % NUM_JUGADORES;
    num_listos++;
}

int siguiente_turno()
{
    if (num_listos == 0)
    {
        return -1; // Cola vacía
    }
    int id = cola_listos[frente];
    frente = (frente + 1) % NUM_JUGADORES;
    num_listos--; // Actualizar el número de jugadores en la cola
    return id;
}

void reiniciar_cola_listos()
{
    // Reiniciar los índices de la cola circular
    frente = 0;
    final = 0;
    num_listos = 0; // Reiniciar el contador de jugadores en la cola

    // Agregar todos los jugadores en juego a la cola de listos
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        if (jugadores[i].en_juego)
        {
            cola_listos[final] = jugadores[i].id;
            final = (final + 1) % NUM_JUGADORES;
            num_listos++;
        }
    }
    printf("Cola de listos reiniciada. Jugadores en cola: %d\n", num_listos);
}

void mover_a_cola_bloqueados(int id_jugador) {
    estados_jugadores[id_jugador-1].estado = BLOQUEADO;
    estados_jugadores[id_jugador-1].tiempo_bloqueado = rand() % 10 + 1;
    pcbs[id_jugador - 1].estado = 0;
    printf("Jugador %d bloqueado por %d segundos\n", id_jugador, estados_jugadores[id_jugador-1].tiempo_bloqueado);
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
    int ch;
    int oldf;
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

// FCFS: Ejecuta el turno completo del jugador
void *ejecutarFCFS(void *arg)
{
    jugador_t *jugador = (jugador_t *)arg;
    pthread_mutex_lock(&mutex);
    if (jugador->en_juego)
    {
        printf("Jugador %d juega su turno completo.\n", jugador->id);
        sleep(3);
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

// Round Robin: Ejecuta un turno parcial del jugador
void *ejecutarRoundRobin(void *arg)
{
    jugador_t *jugador = (jugador_t *)arg;
    pthread_mutex_lock(&mutex);
    if (jugador->en_juego)
    {
        int tiempo_juego = (jugador->tiempo_restante > QUANTUM) ? QUANTUM : jugador->tiempo_restante;
        printf("Jugador %d juega por %d unidades de tiempo.\n", jugador->id, tiempo_juego);
        sleep(tiempo_juego);
        jugador->tiempo_restante -= tiempo_juego;
        if (jugador->tiempo_restante <= 0)
        {
            jugador->en_juego = false;
        }
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
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

        int jugador_id;

        // Seleccionar el jugador según el modo de planificación
        if (modo == 'F') // FCFS
        {
            jugador_id = cola_listos[frente]; // Tomar el jugador al frente de la cola
            frente = (frente + 1) % NUM_JUGADORES; // Avanzar el frente
        }
        else if (modo == 'R') // Round Robin
        {
            jugador_id = cola_listos[frente]; // Tomar el jugador al frente de la cola
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

// ----------------------------------------------------------------------
// Función Principal
// ----------------------------------------------------------------------
int main()
{
    // Inicialización del juego
    srand(time(NULL)); // Para generación de números aleatorios

    // 1. Inicializar mazo y banco de apeadas    
    inicializar_mazo(&mazo);
    barajar_mazo(&mazo);
    banco_inicializar(&banco_apeadas);

    // 2. Inicializar jugadores
    inicializar_jugadores(&mazo);

    // 3. Configuración inicial de jugadores
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        char nombre[MAX_NOMBRE];
        printf("Ingrese nombre para Jugador %d: ", i + 1);
        scanf("%49s", nombre);
        strcpy(jugadores[i].nombre, nombre);
        jugadores[i].id = i + 1;
        jugadores[i].en_juego = true;
        jugadores[i].carta_agregada = false;

        // Inicializar PCB
        pcbs[i].id_jugador = i + 1;
        strcpy(pcbs[i].nombre, nombre);
        pcbs[i].cartas_en_mano = CARTAS_INICIALES;
        pcbs[i].estado = 1; // 1 = listo
        escribir_pcb(pcbs[i]);
    }

    // 4. Preparar cola de jugadores listos
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        agregar_a_cola_listos(jugadores[i].id);
    }
    turno_actual = cola_listos[frente];

    // 5. Hilo para manejar E/S (jugadores bloqueados)
    pthread_t hilo_es;
    pthread_create(&hilo_es, NULL, (void *)verificar_cola_bloqueados, NULL);

    // 6. Bucle principal del juego
    int ganador = -1;
    bool juego_activo = true;

    while (juego_activo)
    {
        // Verificar si hay ganador
        ganador = determinar_ganador(jugadores, NUM_JUGADORES, mazo.cantidad == 0);
        if (ganador != -1)
        {
            printf("\n¡Jugador %d (%s) ha ganado la partida!\n",
                jugadores[ganador].id, jugadores[ganador].nombre);
            juego_activo = false;
            break;
        }

        // Verificar si se presionó una tecla para cambiar el modo
        if (kbhit())
        {
            char tecla = getchar();
            if (tecla == 'F' || tecla == 'f')
            {
                modo = 'F';
                printf("\nModo cambiado a FCFS\n");
            }
            else if (tecla == 'R' || tecla == 'r')
            {
                modo = 'R';
                printf("\nModo cambiado a Round Robin\n");
            }
        }

        // Manejo de turnos
        if (turno_actual != -1)
        {
            jugador_t *jugador_actual = &jugadores[turno_actual - 1];

            printf("\n=== Turno de %s ===\n", jugador_actual->nombre);
            printf("Cartas en mano (%d):\n", jugador_actual->mano.cantidad);
            mostrar_mano(&jugador_actual->mano);

            // Menú de acciones
            int opcion;
            printf("\nOpciones:\n");
            printf("1. Robar carta del mazo\n");
            printf("2. Hacer apeada\n");
            printf("3. Embonar carta\n");
            printf("4. Descartar carta\n");
            printf("5. Pasar turno\n");
            printf("Seleccione: ");
            scanf("%d", &opcion);

            pthread_mutex_lock(&mutex_mesa);

            switch (opcion)
            {
            case 1: // Robar carta
                if (mazo.cantidad > 0)
                {
                    carta_t robada = mazo.cartas[--mazo.cantidad];
                    agregar_carta(&(jugador_actual->mano), robada);
                    pcbs[turno_actual - 1].cartas_robadas++;
                    printf("Robaste: %d de %s\n", robada.numero, robada.color);
                }
                else
                {
                    printf("¡Mazo vacío!\n");
                }
                break;

            case 2: // Apeada
            if (jugador_actual->mano.cantidad > 0)
            {
                realizar_apeada_optima(jugador_actual, &banco_apeadas);
        
                // Verificar si la apeada fue válida
                if (jugador_actual->puntos_suficientes)
                {
                    printf("Apeada válida realizada. Turno finalizado.\n");
        
                    // Pasar el turno al siguiente jugador
                    turno_actual = siguiente_turno();
                    if (turno_actual == -1)
                    {
                        printf("Todos los jugadores han pasado. Reiniciando turnos.\n");
                        reiniciar_cola_listos(); // Reiniciar la cola de listos
                        turno_actual = siguiente_turno(); // Obtener el primer jugador de la nueva cola
                    }
                }
                else
                {
                    // Si no tiene apeadas válidas, robar una carta
                    if (mazo.cantidad > 0)
                    {
                        carta_t robada = mazo.cartas[--mazo.cantidad];
                        agregar_carta(&(jugador_actual->mano), robada);
                        pcbs[turno_actual - 1].cartas_robadas++;
                        printf("No se pudo realizar una apeada válida. Robaste: %d de %s\n", robada.numero, robada.color);
                    }
                    else
                    {
                        printf("¡Mazo vacío! No se puede robar carta.\n");
                    }
        
                    // Pasar el turno al siguiente jugador
                    turno_actual = siguiente_turno();
                    if (turno_actual == -1)
                    {
                        printf("Todos los jugadores han pasado. Reiniciando turnos.\n");
                        reiniciar_cola_listos(); // Reiniciar la cola de listos
                        turno_actual = siguiente_turno(); // Obtener el primer jugador de la nueva cola
                    }
                }
            }
            else
            {
                printf("No tienes cartas para hacer apeada.\n");
            }
            break;

            case 3: // Embonar
                if (banco_apeadas.total_grupos > 0 || banco_apeadas.total_escaleras > 0)
                {
                    printf("Seleccione carta para embonar (1-%d): ", jugador_actual->mano.cantidad);
                    int carta_idx;
                    scanf("%d", &carta_idx);
                    if (embonar_carta(jugador_actual, &banco_apeadas, carta_idx - 1))
                    {
                        printf("Carta embonada exitosamente!\n");
                    }
                    else
                    {
                        printf("No se pudo embonar la carta\n");
                    }
                }
                else
                {
                    printf("No hay combinaciones en la mesa para embonar\n");
                }
                break;

            case 4: // Descartar
                if (jugador_actual->mano.cantidad > 0)
                {
                    printf("Seleccione carta a descartar (1-%d): ", jugador_actual->mano.cantidad);
                    int descarte_idx;
                    scanf("%d", &descarte_idx);
                    remover_carta(&jugador_actual->mano, descarte_idx - 1);
                    pcbs[turno_actual - 1].cartas_descartadas++;
                    printf("Carta descartada\n");
                }
                break;

            case 5: // Pasar
                printf("Turno pasado\n");

                // Rotar turno al siguiente jugador
                turno_actual = siguiente_turno();
                if (turno_actual == -1)
                {
                    printf("Todos los jugadores han pasado. Reiniciando turnos.\n");
                    reiniciar_cola_listos(); // Reiniciar la cola de listos
                    turno_actual = siguiente_turno(); // Obtener el primer jugador de la nueva cola
                }
                break;                

            default:
                printf("Opción inválida\n");
            }

            // Finalizar turno
            finalizar_turno(jugador_actual);
            pcbs[turno_actual - 1].turnos_jugados++;
            pcbs[turno_actual - 1].cartas_en_mano = jugador_actual->mano.cantidad;

            // Rotar turno
            turno_actual = siguiente_turno();
            if (turno_actual == -1)
            {
                printf("Todos los jugadores han pasado. Reiniciando turnos.\n");
                reiniciar_cola_listos(); // Reiniciar la cola de listos
                turno_actual = siguiente_turno(); // Obtener el primer jugador de la nueva cola
            }

            pthread_mutex_unlock(&mutex_mesa);

            // Pequeña pausa entre turnos
            sleep(1);
        }
    }

    // 7. Finalización del juego
    printf("\n=== Resultados Finales ===\n");
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        printf("Jugador %d (%s): %d puntos\n",
               jugadores[i].id, jugadores[i].nombre,
               calcular_puntos_mano(&jugadores[i].mano));
    }

    // 8. Liberar recursos
    banco_liberar(&banco_apeadas);
    liberar_jugadores();
    pthread_cancel(hilo_es);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex_mesa);

    return 0;
}