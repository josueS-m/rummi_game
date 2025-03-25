#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h> 
#include <unistd.h>

#define MAX_CARTAS 108
#define MAX_MANO 14
#define NUM_JUGADORES 4
#define QUANTUM 5 // Tiempo por turno en Round Robinson
#define PUNTOS_MINIMOS_APEADA 30

//<----------Estructuras----------->

typedef struct {
    int numero;
    char color[10];
} carta_t;

typedef struct {
    carta_t cartas[MAX_CARTAS];
    int cantidad;
} mazo_t;

typedef struct {
    carta_t cartas[MAX_MANO];
    int cantidad;
} mano_t;

typedef struct {
    mano_t mano;
    char nombre[20];
    int id;
} jugador_t;

typedef struct {
    carta_t cartas[4];
    int cantidad;
} grupo_t;

typedef struct {
    carta_t cartas[13];
    int cantidad;
} escalera_t;

typedef struct {
    grupo_t grupos[10];
    escalera_t escaleras[10];
    int total_grupos;
    int total_escaleras;
} apeadas_t;

typedef struct {
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
    int tiempo_restante; //Para tiempo restante del bloqueo
} pcb_t;

// ---------------------- Variables Globales para Concurrencia ----------------------

pthread_mutex_t mutex_mesa = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger la mesa
int cola_listos[NUM_JUGADORES]; // Cola de jugadores listos para actuar
int frente = 0, final = 0; // Índices para la cola de listos
int cola_bloqueados[NUM_JUGADORES]; // Cola de jugadores bloqueados
int num_bloqueados = 0; // Número de jugadores bloqueados
int turno_actual = 0; // ID del jugador con turno actual

//Variable globales para jugadores y PCBs
jugador_t jugadores[NUM_JUGADORES];
pcb_t pcbs[NUM_JUGADORES];

//<------Logica del juego--------->

int es_grupo_valido(carta_t cartas[], int cantidad) {
    int numero_base = -1;
    for (int i = 0; i < cantidad; i++) {
        if (cartas[i].numero != 0) {
            if (numero_base == -1)
                numero_base = cartas[i].numero;
            else if (cartas[i].numero != numero_base)
                return 0;
        }
    }
    return (numero_base != -1);
}

int es_escalera_valida(carta_t cartas[], int cantidad) {
    int nums[3], idx = 0;
    char color_base[10] = "";

    for (int i = 0; i < cantidad; i++) {
        if (cartas[i].numero != 0) {
            nums[idx++] = cartas[i].numero;
            if (strlen(color_base) == 0)
                strcpy(color_base, cartas[i].color);
            else if (strcmp(color_base, cartas[i].color) != 0)
                return 0;
        }
    }

    if (idx == 0) return 0;

    // Ordenar números
    for (int i = 0; i < idx-1; i++) {
        for (int j = i+1; j < idx; j++) {
            if (nums[i] > nums[j]) {
                int temp = nums[i];
                nums[i] = nums[j];
                nums[j] = temp;
            }
        }
    }

    int huecos = 0;
    for (int i = 0; i < idx-1; i++) {
        huecos += (nums[i+1] - nums[i] - 1);
    }

    int comodines = cantidad - idx;
    return (huecos <= comodines);
}

int calcular_puntos_grupo(carta_t grupo[], int cantidad) {
    int valor_base = -1;
    for (int i = 0; i < cantidad; i++) {
        if (grupo[i].numero != 0) {
            valor_base = grupo[i].numero;
            break;
        }
    }
    if (valor_base == -1) return 0;
    return cantidad * valor_base;
}

int calcular_puntos_escalera(carta_t escalera[], int cantidad) {
    int puntos = 0;
    int nonwild[3], idx = 0;
    int num_wild = 0;

    for (int i = 0; i < cantidad; i++) {
        if (escalera[i].numero == 0) {
            num_wild++;
        } else {
            nonwild[idx++] = escalera[i].numero;
            puntos += escalera[i].numero;
        }
    }

    if (num_wild > 0 && idx > 0) {
        int max_val = nonwild[0];
        for (int i = 1; i < idx; i++) {
            if (nonwild[i] > max_val) max_val = nonwild[i];
        }
        puntos += num_wild * (max_val + 1);
    }
    return puntos;
}

void verificar_apeadas(jugador_t *jugador) {
    mano_t *mano = &(jugador->mano);
    int puntos_apeada = 0;
    int cartas_apeadas = 0;

    // Buscar grupos
    for (int i = 0; i < mano->cantidad - 2; i++) {
        for (int j = i+1; j < mano->cantidad - 1; j++) {
            for (int k = j+1; k < mano->cantidad; k++) {
                carta_t grupo[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};
                if (es_grupo_valido(grupo, 3)) {
                    int puntos = calcular_puntos_grupo(grupo, 3);
                    puntos_apeada += puntos;
                    cartas_apeadas += 3;

                    // Agregar al banco de apeadas
                    if (banco_apeadas.total_grupos < 10) {
                        memcpy(banco_apeadas.grupos[banco_apeadas.total_grupos].cartas, grupo, sizeof(grupo));
                        banco_apeadas.grupos[banco_apeadas.total_grupos].cantidad = 3;
                        banco_apeadas.total_grupos++;
                    }

                    // Eliminar cartas
                    remover_carta(mano, k);
                    remover_carta(mano, j);
                    remover_carta(mano, i);
                    pcbs[jugador->id-1].grupos_formados++;
                    i = -1;
                    break;
                }
            }
        }
    }

    // Buscar escaleras
    for (int i = 0; i < mano->cantidad - 2; i++) {
        for (int j = i+1; j < mano->cantidad - 1; j++) {
            for (int k = j+1; k < mano->cantidad; k++) {
                carta_t escalera[3] = {mano->cartas[i], mano->cartas[j], mano->cartas[k]};
                if (es_escalera_valida(escalera, 3)) {
                    int puntos = calcular_puntos_escalera(escalera, 3);
                    puntos_apeada += puntos;
                    cartas_apeadas += 3;

                    // Agregar al banco de apeadas
                    if (banco_apeadas.total_escaleras < 10) {
                        memcpy(banco_apeadas.escaleras[banco_apeadas.total_escaleras].cartas, escalera, sizeof(escalera));
                        banco_apeadas.escaleras[banco_apeadas.total_escaleras].cantidad = 3;
                        banco_apeadas.total_escaleras++;
                    }

                    // Eliminar cartas
                    remover_carta(mano, k);
                    remover_carta(mano, j);
                    remover_carta(mano, i);
                    pcbs[jugador->id-1].escaleras_formadas++;
                    i = -1;
                    break;
                }
            }
        }
    }

    if (puntos_apeada >= PUNTOS_MINIMOS_APEADA) {
        printf("Jugador %d hizo apeada válida! Puntos: %d\n", jugador->id, puntos_apeada);
        pcbs[jugador->id-1].puntos += puntos_apeada;
    }
}

int determinar_ganador() {
    int ganador = -1;
    int min_cartas = MAX_MANO + 1;

    for (int i = 0; i < NUM_JUGADORES; i++) {
        if (jugadores[i].mano.cantidad < min_cartas) {
            min_cartas = jugadores[i].mano.cantidad;
            ganador = i;
        }
    }

    if (ganador != -1) {
        printf("\n¡Jugador %d - %s gana con %d cartas restantes!\n",
               jugadores[ganador].id, jugadores[ganador].nombre, jugadores[ganador].mano.cantidad);
        pcbs[ganador].partidas_ganadas++;
        if (pcbs[ganador].escaleras_formadas > 0) {
            pcbs[ganador].victorias_con_escalera = 1;
        }
    }

    return ganador;
}

//<------Fin de la logica del juego--------->
		      
// ---------------------- Funciones para Mazo -----------------------

void inicializar_mazo(mazo_t *mazo) {
    const char *colores[] = {"rojo", "negro", "azul", "amarillo"};
    int index = 0;
    
    for (int k = 0; k < 2; k++) {
        for (int c = 0; c < 4; c++) {
            for (int n = 1; n <= 13; n++) {
                mazo->cartas[index].numero = n;
                strcpy(mazo->cartas[index].color, colores[c]);
                index++;
            }
        }
    }
    for (int j = 0; j < 4; j++) {
        mazo->cartas[index].numero = 0;
        strcpy(mazo->cartas[index].color, "comodin");
        index++;
    }
    mazo->cantidad = index;
}

void barajar_mazo(mazo_t *mazo) {
    srand(time(NULL));
    for (int i = 0; i < mazo->cantidad; i++) {
        int j = rand() % mazo->cantidad;
        carta_t temp = mazo->cartas[i];
        mazo->cartas[i] = mazo->cartas[j];
        mazo->cartas[j] = temp;
    }
}

// ---------------------- Funciones para Mano -----------------------

void agregar_carta(mano_t *mano, carta_t carta) {
    if (mano->cantidad < MAX_MANO) {
        mano->cartas[mano->cantidad] = carta;
        mano->cantidad++;
    }
}

void remover_carta(mano_t *mano, int pos) {
    if (pos >= 0 && pos < mano->cantidad) {
        for (int i = pos; i < mano->cantidad - 1; i++) {
            mano->cartas[i] = mano->cartas[i + 1];
        }
        mano->cantidad--;
    }
}

void mostrar_mano(mano_t *mano) {
    printf("Cartas en mano (%d):\n", mano->cantidad);
    for (int i = 0; i < mano->cantidad; i++) {
        if (mano->cartas[i].numero == 0) {
            printf("Comodin\n");
        } else {
            printf("%d de %s\n", mano->cartas[i].numero, mano->cartas[i].color);
        }
    }
}

// ---------------------- Inicialización Jugadores y PCB -----------------------

void inicializar_jugador(jugador_t *jugador, int id, char nombre[], carta_t cartas[], int cantidad) {
    jugador->id = id;
    strcpy(jugador->nombre, nombre);
    jugador->mano.cantidad = 0;
    for (int i = 0; i < cantidad; i++) {
        agregar_carta(&(jugador->mano), cartas[i]);
    }
}

void actualizar_pcb(pcb_t *pcb, jugador_t *jugador) {
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
    pcb->tiempo_restante=0;
}

// ---------------------- Escritura PCB y Tabla Procesos -----------------------

void escribir_pcb(pcb_t jugador) {
    char filename[30];
    sprintf(filename, "PCB_Jugador%d.txt", jugador.id_jugador);

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
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

void actualizar_tabla_procesos(pcb_t jugadores[], int num_jugadores) {
    FILE *file = fopen("tabla_procesos.txt", "w");
    if (file == NULL) {
        printf("Error al abrir tabla_procesos.txt\n");
        return;
    }

    fprintf(file, "ID\tNombre\tCartas\tPuntos\tEstado\n");
    for (int i = 0; i < num_jugadores; i++) {
        fprintf(file, "%d\t%s\t%d\t%d\t%d\n",
                jugadores[i].id_jugador, jugadores[i].nombre,
                jugadores[i].cartas_en_mano, jugadores[i].puntos,
                jugadores[i].estado);
    }
    fclose(file);
}

// ---------------------- Repartir Cartas -----------------------

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
    mazo->cantidad -= index;
}

// <---------------------- Módulo de Concurrencia e Hilos ---------------------->
// Este módulo implementa la lógica de hilos, sincronización y planificación.

// Función para agregar jugadores a la cola de listos
void agregar_a_cola_listos(int id_jugador) {
    cola_listos[final] = id_jugador;
    final = (final + 1) % NUM_JUGADORES;
}

// Función para obtener el siguiente jugador en la cola de listos
int siguiente_turno() {
    if (frente == final) return -1; // Cola vacía
    int id = cola_listos[frente];
    frente = (frente + 1) % NUM_JUGADORES;
    return id;
}

// Función para mover jugadores a la cola de bloqueados
void mover_a_cola_bloqueados(int id_jugador) {
    cola_bloqueados[num_bloqueados] = id_jugador;
    num_bloqueados++;
    pcbs[id_jugador - 1].estado = 0; // Bloqueado
    pcbs[id_jugador - 1].tiempo_restante = rand() % 10 + 1; // Tiempo aleatorio
}

// Función para verificar y reactivar jugadores bloqueados
void verificar_cola_bloqueados() {
    for (int i = 0; i < num_bloqueados; i++) {
        pcbs[cola_bloqueados[i] - 1].tiempo_restante--;
        if (pcbs[cola_bloqueados[i] - 1].tiempo_restante <= 0) {
            agregar_a_cola_listos(cola_bloqueados[i]);
            pcbs[cola_bloqueados[i] - 1].estado = 1; // Listo
            num_bloqueados--;
        }
    }
}

// Función del hilo del jugador
void* jugador_thread(void* arg) {
    jugador_t* jugador = (jugador_t*)arg;
    pcb_t* pcb = &pcbs[jugador->id - 1];

    while (1) {
        pthread_mutex_lock(&mutex_mesa); // Bloquear acceso a la mesa

        if (turno_actual == jugador->id) {
            printf("Turno del Jugador %d - %s\n", jugador->id, jugador->nombre);

            // Simular acción del jugador
            if (jugador->mano.cantidad > 0) {
                printf("Jugador %d baja una carta.\n", jugador->id);
                jugador->mano.cantidad--;
                pcb->cartas_descartadas++;
            } else {
                printf("Jugador %d no tiene cartas. Bloqueado.\n", jugador->id);
                mover_a_cola_bloqueados(jugador->id);
            }

            // Actualizar PCB
            pcb->cartas_en_mano = jugador->mano.cantidad;
            pcb->estado = 1; // Listo

            // Cambiar turno
            turno_actual = siguiente_turno();
            if (turno_actual == -1) break; // Fin del juego
        }

        pthread_mutex_unlock(&mutex_mesa); // Liberar acceso a la mesa
        sleep(1); // Simular tiempo de procesamiento
    }

    return NULL;
}

// Función principal para iniciar hilos
void iniciar_concurrencia() {
    pthread_t hilos[NUM_JUGADORES];

    // Crear hilos para cada jugador
    for (int i = 0; i < NUM_JUGADORES; i++) {
        pthread_create(&hilos[i], NULL, jugador_thread, &jugadores[i]);
    }

    // Esperar a que los hilos terminen
    for (int i = 0; i < NUM_JUGADORES; i++) {
        pthread_join(hilos[i], NULL);
    }

    printf("Juego terminado.\n");
}

// <---------------------- Fin del Módulo de Concurrencia e Hilos ---------------------->

// ---------------------- MAIN -----------------------

int main() {
    mazo_t mazo;
    jugador_t jugadores[NUM_JUGADORES];
    pcb_t pcbs[NUM_JUGADORES];

    inicializar_mazo(&mazo);
    barajar_mazo(&mazo);

    repartir_cartas(jugadores, NUM_JUGADORES, &mazo);

    for (int i = 0; i < NUM_JUGADORES; i++) {
        char nombre[20];
        printf("Ingrese el nombre del Jugador %d: ", i + 1);
        scanf("%19s", nombre);

        inicializar_jugador(&jugadores[i], i + 1, nombre, jugadores[i].mano.cartas, jugadores[i].mano.cantidad);
        actualizar_pcb(&pcbs[i], &jugadores[i]);

        printf("Jugador %d - %s\n", jugadores[i].id, jugadores[i].nombre);
        mostrar_mano(&(jugadores[i].mano));
        escribir_pcb(pcbs[i]);
        printf("---------------------------------------\n");
    }

    // Inicializar turno_actual y cola de listos
    turno_actual = 1; // Iniciar con el Jugador 1
    for (int i = 0; i < NUM_JUGADORES; i++) {
        agregar_a_cola_listos(jugadores[i].id);
    }

    actualizar_tabla_procesos(pcbs, NUM_JUGADORES);

    // Iniciar concurrencia e hilos
    iniciar_concurrencia();

    printf("Juego inicializado correctamente.\n");
    return 0;
}
