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

void iniciar_concurrencia(); // Prototipo de la función
void* jugador_thread(void* arg); // Prototipo de la funciónlea

// ----------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------
#define NUM_JUGADORES 4
#define MAX_CARTAS 108
#define MAX_MANO 14 // Máximo de cartas en mano
#define QUANTUM 5   // Tiempo por turno en Round Robin
#define PUNTOS_MINIMOS_APEADA 30

#define MAX_CARTAS_GRUPO 10 // Tamaño máximo de un grupo o escalera
#define MAX_GRUPOS 10       // Máximo grupos en banco de apeadas
#define MAX_ESCALERAS 10    // Máximo escaleras en banco de apeadas

// ----------------------------------------------------------------------
// Estructuras y Tipos
// ----------------------------------------------------------------------

// Carta
typedef struct
{
    int numero;
    char color[10];
} carta_t;

// Mano (lista de cartas del jugador)
typedef struct
{
    carta_t *cartas;
    int cantidad;
    int capacidad;
} mano_t;

// Grupo y Escalera: para simplificar, se usa el mismo tipo
typedef struct
{
    carta_t cartas[MAX_CARTAS_GRUPO];
    int cantidad;
} grupo_t, escalera_t;

// Banco de apeadas: almacena grupos y escaleras formados
typedef struct
{
    grupo_t grupos[MAX_GRUPOS];
    escalera_t escaleras[MAX_ESCALERAS];
    int total_grupos;
    int total_escaleras;
} banco_de_apeadas_t;

// Jugador
typedef struct
{
    mano_t mano;
    char nombre[20];
    int id;
    int tiempo_restante;
    bool en_juego;
} jugador_t;

// PCB para seguimiento de jugador (estadísticas)
typedef struct
{
    int id_jugador;
    char nombre[20];
    int cartas_en_mano;
    int puntos;
    int partidas_jugadas;
    int partidas_ganadas;
    int partidas_perdidas;
    int estado;
    int tiempo_total_juego;
    int turnos_jugados;
    int cartas_robadas;
    int cartas_descartadas;
    int grupos_formados;
    int escaleras_formadas;
    int victorias_con_escalera;
    int tiempo_restante;
} pcb_t;

// Mazo de cartas
typedef struct
{
    carta_t cartas[MAX_CARTAS];
    int cantidad;
} mazo_t;

// ----------------------------------------------------------------------
// Variables Globales Únicas
// ----------------------------------------------------------------------
jugador_t jugadores[NUM_JUGADORES];
pcb_t pcbs[NUM_JUGADORES];
banco_de_apeadas_t banco_apeadas = {.total_grupos = 0, .total_escaleras = 0};
int turno_actual = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_mesa = PTHREAD_MUTEX_INITIALIZER;
char modo = 'F'; // Modo inicial: FCFS

// Variables para la cola de turnos y bloqueo
int cola_listos[NUM_JUGADORES];
int frente = 0, final = 0;
int cola_bloqueados[NUM_JUGADORES];
int num_bloqueados = 0;

// ----------------------------------------------------------------------
// Funciones de Validación y Cálculo de Puntos
// ----------------------------------------------------------------------
int es_grupo_valido(carta_t cartas[], int cantidad)
{
    int numero_base = -1;
    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero != 0)
        {
            if (numero_base == -1)
                numero_base = cartas[i].numero;
            else if (cartas[i].numero != numero_base)
                return 0;
        }
    }
    return (numero_base != -1);
}

int es_escalera_valida(carta_t cartas[], int cantidad)
{
    int nums[3], idx = 0;
    char color_base[10] = "";

    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero != 0)
        {
            nums[idx++] = cartas[i].numero;
            if (strlen(color_base) == 0)
                strcpy(color_base, cartas[i].color);
            else if (strcmp(color_base, cartas[i].color) != 0)
                return 0;
        }
    }

    if (idx == 0)
        return 0;

    // Ordenar números
    for (int i = 0; i < idx - 1; i++)
    {
        for (int j = i + 1; j < idx; j++)
        {
            if (nums[i] > nums[j])
            {
                int temp = nums[i];
                nums[i] = nums[j];
                nums[j] = temp;
            }
        }
    }

    int huecos = 0;
    for (int i = 0; i < idx - 1; i++)
    {
        huecos += (nums[i + 1] - nums[i] - 1);
    }

    int comodines = cantidad - idx;
    return (huecos <= comodines);
}

int calcular_puntos_grupo(carta_t grupo[], int cantidad)
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
        return 0;
    return cantidad * valor_base;
}

int calcular_puntos_escalera(carta_t escalera[], int cantidad)
{
    int puntos = 0;
    int nonwild[3], idx = 0;
    int num_wild = 0;

    for (int i = 0; i < cantidad; i++)
    {
        if (escalera[i].numero == 0)
        {
            num_wild++;
        }
        else
        {
            nonwild[idx++] = escalera[i].numero;
            puntos += escalera[i].numero;
        }
    }

    if (num_wild > 0 && idx > 0)
    {
        int max_val = nonwild[0];
        for (int i = 1; i < idx; i++)
        {
            if (nonwild[i] > max_val)
                max_val = nonwild[i];
        }
        puntos += num_wild * (max_val + 1);
    }
    return puntos;
}

// ----------------------------------------------------------------------
// Funciones de Apeadas
// ----------------------------------------------------------------------
void remover_carta(mano_t *mano, int pos); // Prototipo para poder usarla en verificar_apeadas

void verificar_apeadas(jugador_t *jugador)
{
    mano_t *mano = &(jugador->mano);
    int puntos_apeada = 0;
    int cartas_apeadas = 0;

    // Buscar grupos en la mano
    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                carta_t grupo[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};
                if (es_grupo_valido(grupo, 3))
                {
                    int puntos = calcular_puntos_grupo(grupo, 3);
                    puntos_apeada += puntos;
                    cartas_apeadas += 3;

                    // Agregar grupo al banco de apeadas
                    if (banco_apeadas.total_grupos < MAX_GRUPOS)
                    {
                        memcpy(banco_apeadas.grupos[banco_apeadas.total_grupos].cartas, grupo, sizeof(grupo));
                        banco_apeadas.grupos[banco_apeadas.total_grupos].cantidad = 3;
                        banco_apeadas.total_grupos++;
                    }

                    // Eliminar cartas de la mano
                    remover_carta(mano, k);
                    remover_carta(mano, j);
                    remover_carta(mano, i);
                    pcbs[jugador->id - 1].grupos_formados++;
                    i = -1;
                    break;
                }
            }
        }
    }

    // Buscar escaleras en la mano
    for (int i = 0; i < mano->cantidad - 2; i++)
    {
        for (int j = i + 1; j < mano->cantidad - 1; j++)
        {
            for (int k = j + 1; k < mano->cantidad; k++)
            {
                carta_t escalera[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};
                if (es_escalera_valida(escalera, 3))
                {
                    int puntos = calcular_puntos_escalera(escalera, 3);
                    puntos_apeada += puntos;
                    cartas_apeadas += 3;

                    // Agregar escalera al banco de apeadas
                    if (banco_apeadas.total_escaleras < MAX_ESCALERAS)
                    {
                        memcpy(banco_apeadas.escaleras[banco_apeadas.total_escaleras].cartas, escalera, sizeof(escalera));
                        banco_apeadas.escaleras[banco_apeadas.total_escaleras].cantidad = 3;
                        banco_apeadas.total_escaleras++;
                    }

                    // Eliminar cartas de la mano
                    remover_carta(mano, k);
                    remover_carta(mano, j);
                    remover_carta(mano, i);
                    pcbs[jugador->id - 1].escaleras_formadas++;
                    i = -1;
                    break;
                }
            }
        }
    }

    if (puntos_apeada >= PUNTOS_MINIMOS_APEADA)
    {
        printf("Jugador %d hizo apeada válida! Puntos: %d\n", jugador->id, puntos_apeada);
        pcbs[jugador->id - 1].puntos += puntos_apeada;
    }
}

int determinar_ganador() {
    for (int i = 0; i < NUM_JUGADORES; i++) {
        if (jugadores[i].mano.cantidad == 0 && pcbs[i].cartas_robadas == 0) {
            printf("\n¡Jugador %d - %s gana!\n", jugadores[i].id, jugadores[i].nombre);
            return i;
        }
    }
    return -1; // No hay ganador aún
}

int determinar_ganador_por_puntaje() {
    int ganador = -1;
    int menor_puntaje = INT_MAX;
    for (int i = 0; i < NUM_JUGADORES; i++) {
        if (pcbs[i].puntos < menor_puntaje) {
            menor_puntaje = pcbs[i].puntos;
            ganador = i;
        }
    }
    return ganador;
}

// ----------------------------------------------------------------------
// Funciones para el Mazo
// ----------------------------------------------------------------------
void inicializar_mazo(mazo_t *mazo)
{
    const char *colores[] = {"rojo", "negro", "azul", "amarillo"};
    int index = 0;

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
    for (int j = 0; j < 4; j++)
    {
        mazo->cartas[index].numero = 0;
        strcpy(mazo->cartas[index].color, "comodin");
        index++;
    }
    mazo->cantidad = index;
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
void agregar_carta(mano_t *mano, carta_t carta)
{
    if (mano->cantidad < MAX_MANO)
    {
        mano->cartas[mano->cantidad] = carta;
        mano->cantidad++;
    }
}

void remover_carta(mano_t *mano, int pos)
{
    if (pos >= 0 && pos < mano->cantidad)
    {
        for (int i = pos; i < mano->cantidad - 1; i++)
        {
            mano->cartas[i] = mano->cartas[i + 1];
        }
        mano->cantidad--;
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
void repartir_cartas(jugador_t jugadores[], int num_jugadores, mazo_t *mazo) {
    int cartas_por_jugador = (mazo->cantidad * 2) / (3 * num_jugadores);
    int index = 0;
    for (int i = 0; i < num_jugadores; i++) {
        jugadores[i].mano.cantidad = 0;
        for (int j = 0; j < cartas_por_jugador; j++) {
            agregar_carta(&(jugadores[i].mano), mazo->cartas[index]);
            index++;
        }
    }
    mazo->cantidad -= index; // El resto queda en la banca
}

// ----------------------------------------------------------------------
// Funciones de Concurrencia y Manejo de Turnos
// ----------------------------------------------------------------------
void agregar_a_cola_listos(int id_jugador)
{
    cola_listos[final] = id_jugador;
    final = (final + 1) % NUM_JUGADORES;
}

int siguiente_turno() {
    if (frente == final) {
        return -1; // Cola vacía
    }
    int id = cola_listos[frente];
    frente = (frente + 1) % NUM_JUGADORES;
    return id;
}

void mover_a_cola_bloqueados(int id_jugador)
{
    cola_bloqueados[num_bloqueados] = id_jugador;
    num_bloqueados++;
    pcbs[id_jugador - 1].estado = 0;                        // Bloqueado
    pcbs[id_jugador - 1].tiempo_restante = rand() % 10 + 1; // Tiempo aleatorio
}

void verificar_cola_bloqueados() {
    for (int i = 0; i < num_bloqueados; i++) {
        pcbs[cola_bloqueados[i] - 1].tiempo_restante--;
        if (pcbs[cola_bloqueados[i] - 1].tiempo_restante <= 0) {
            printf("Jugador %d ha terminado su tiempo en E/S. Moviendo a la cola de listos.\n", cola_bloqueados[i]);
            agregar_a_cola_listos(cola_bloqueados[i]);
            pcbs[cola_bloqueados[i] - 1].estado = 1; // Listo

            // Eliminar jugador de la cola de bloqueados
            for (int j = i; j < num_bloqueados - 1; j++) {
                cola_bloqueados[j] = cola_bloqueados[j + 1];
            }
            num_bloqueados--;
            i--; // Ajustar índice después de eliminar
        }
    }
}

void iniciar_concurrencia() {
    pthread_t hilos[NUM_JUGADORES];

    // Crear un hilo para cada jugador
    for (int i = 0; i < NUM_JUGADORES; i++) {
        pthread_create(&hilos[i], NULL, jugador_thread, (void *)&jugadores[i]);
    }

    // Esperar a que todos los hilos terminen
    for (int i = 0; i < NUM_JUGADORES; i++) {
        pthread_join(hilos[i], NULL);
    }
}

// ----------------------------------------------------------------------
// Función del hilo del jugador
// ----------------------------------------------------------------------
void* jugador_thread(void* arg) {
    jugador_t* jugador = (jugador_t*)arg;
    pcb_t* pcb = &pcbs[jugador->id - 1];

    while (1) {
        pthread_mutex_lock(&mutex_mesa); // Bloquear acceso a la mesa

        if (turno_actual == jugador->id) {
            printf("Turno del Jugador %d - %s\n", jugador->id, jugador->nombre);

            // Verificar si el jugador puede apearse
            verificar_apeadas(jugador); // Calcula puntos y apeadas válidas

            if (pcb->puntos >= PUNTOS_MINIMOS_APEADA) {
                printf("Jugador %d puede apearse con %d puntos.\n", jugador->id, pcb->puntos);
            } else {
                printf("Jugador %d no tiene suficientes puntos para apearse.\n", jugador->id);
            }

            // Simular acción del jugador
            if (jugador->mano.cantidad > 0) {
                printf("Jugador %d, elige la posición de la carta que deseas bajar (1-%d):\n", jugador->id, jugador->mano.cantidad);
                mostrar_mano(&(jugador->mano));

                int posicion;
                scanf("%d", &posicion);

                // Validar la posición ingresada
                if (posicion >= 1 && posicion <= jugador->mano.cantidad) {
                    printf("Jugador %d baja la carta en la posición %d.\n", jugador->id, posicion);
                    remover_carta(&(jugador->mano), posicion - 1); // Restar 1 porque las posiciones en el array empiezan en 0
                    pcb->cartas_descartadas++;
                } else {
                    printf("Posición inválida. No se baja ninguna carta.\n");
                }
            } else {
                printf("Jugador %d no tiene cartas. Moviendo a E/S.\n", jugador->id);
                mover_a_cola_bloqueados(jugador->id);
            }

            // Actualizar PCB
            pcb->cartas_en_mano = jugador->mano.cantidad;
            pcb->estado = 1; // Listo

            // Reinserta al jugador en la cola si sigue en el juego
            if (jugador->mano.cantidad > 0) {
                agregar_a_cola_listos(jugador->id);
            }

            // Cambiar turno al siguiente jugador
            turno_actual = siguiente_turno();
            if (turno_actual == -1) {
                printf("No hay más jugadores en la cola. Fin del juego.\n");
                pthread_mutex_unlock(&mutex_mesa);
                break; // Fin del juego
            }
        }

        pthread_mutex_unlock(&mutex_mesa); // Liberar acceso a la mesa
        sleep(1); // Simular tiempo de procesamiento
    }

    return NULL;
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

        int jugador_id = cola_listos[frente];
        frente = (frente + 1) % NUM_JUGADORES;
        turno_actual = jugador_id;

        printf("Turno del jugador: %s (ID: %d)\n", jugadores[jugador_id - 1].nombre, jugador_id);
        jugadores[jugador_id - 1].tiempo_restante = QUANTUM;

        // Simulación del turno
        while (jugadores[jugador_id - 1].tiempo_restante > 0)
        {
            sleep(1);
            jugadores[jugador_id - 1].tiempo_restante--;
        }

        printf("Tiempo agotado para el jugador %s\n", jugadores[jugador_id - 1].nombre);

        // Después del turno, moverlo al final de la cola
        cola_listos[final] = jugador_id;
        final = (final + 1) % NUM_JUGADORES;

        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
    return NULL;
}

// ----------------------------------------------------------------------
// Función para Inicializar Jugadores (única y global)
// ----------------------------------------------------------------------
void inicializar_jugadores()
{
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        jugadores[i].id = i + 1; // ID inicia en 1
        sprintf(jugadores[i].nombre, "Jugador %d", i + 1);
        // Asignar memoria para las cartas de la mano
        jugadores[i].mano.cartas = malloc(sizeof(carta_t) * MAX_MANO);
        jugadores[i].mano.cantidad = 0;
        jugadores[i].mano.capacidad = MAX_MANO;
        jugadores[i].tiempo_restante = QUANTUM;
        jugadores[i].en_juego = true;
    }
}

// ----------------------------------------------------------------------
// Función Principal
// ----------------------------------------------------------------------
int main()
{
    // Inicializar jugadores (y reservar memoria para su mano)
    inicializar_jugadores();

    // Inicializar mazo y repartir cartas
    mazo_t mazo;
    inicializar_mazo(&mazo);
    barajar_mazo(&mazo);
    repartir_cartas(jugadores, NUM_JUGADORES, &mazo);

    // Solicitar nombres y actualizar PCB de cada jugador
    for (int i = 0; i < NUM_JUGADORES; i++) {
        char nombre[20];
        printf("Ingrese el nombre del Jugador %d: ", i + 1);
        scanf("%19s", nombre);
        // Inicializa el jugador con sus cartas repartidas previamente
        inicializar_jugador(&jugadores[i], i + 1, nombre, jugadores[i].mano.cartas, jugadores[i].mano.cantidad);
        actualizar_pcb(&pcbs[i], &jugadores[i]);
        printf("Jugador %d - %s\n", jugadores[i].id, jugadores[i].nombre);
        mostrar_mano(&(jugadores[i].mano));
        escribir_pcb(pcbs[i]);
        printf("---------------------------------------\n");
    }

    // Inicializar cola de listos y turno inicial
    turno_actual = 1; // Iniciar con el Jugador 1
    for (int i = 0; i < NUM_JUGADORES; i++)
    {
        agregar_a_cola_listos(jugadores[i].id);
    } actualizar_tabla_procesos(pcbs, NUM_JUGADORES);

    // Iniciar hilos de concurrencia para el juego
    iniciar_concurrencia();

    // (Opcional) Algoritmos de planificación FCFS / Round Robin
    pthread_t hilos[NUM_JUGADORES];
    while (1)
    {
        system("clear");
        printf("Presiona F para FCFS, R para Round Robin, Q para salir\n");
        if (kbhit())
        {
            char tecla = getchar();
            if (tecla == 'Q' || tecla == 'q')
                break;
            if (tecla == 'F' || tecla == 'f')
                modo = 'F';
            if (tecla == 'R' || tecla == 'r')
                modo = 'R';
        }

        // Ejecutar el modo seleccionado para cada jugador
        for (int i = 0; i < NUM_JUGADORES; i++)
        {
            if (modo == 'F')
            {
                pthread_create(&hilos[i], NULL, ejecutarFCFS, (void *)&jugadores[i]);
            }
            else
            {
                pthread_create(&hilos[i], NULL, ejecutarRoundRobin, (void *)&jugadores[i]);
            }
            pthread_join(hilos[i], NULL);
        }
        sleep(1);
    }

    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex_mesa);

    printf("Juego terminado.\n");
    return 0;
}
