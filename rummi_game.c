#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CARTAS 108
#define MAX_MANO 14

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
    grupo_t grupos[4];
    escalera_t escaleras[4];
    int total_grupos;
    int total_escaleras;
} apeadas_t;

typedef struct {
	grupo_t *grupos;
	escalera_t *escaleras;
	int total_grupos;
	int total_escaleras;
} banco_de_escaleras_t;


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
} pcb_t;

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

// ---------------------- MAIN -----------------------

int main() {
    mazo_t mazo;
    jugador_t jugadores[4];
    pcb_t pcbs[4];

    inicializar_mazo(&mazo);
    barajar_mazo(&mazo);

    repartir_cartas(jugadores, 4, &mazo);

    for (int i = 0; i < 4; i++) {
        char nombre[20];
        printf("Ingrese el nombre del Jugador %d: ", i + 1);
        scanf("%19s", nombre);

        inicializar_jugador(&jugadores[i], i + 1, nombre, jugadores[i].mano.cartas, jugadores[i].mano.cantidad);
        actualizar_pcb(&pcbs[i], &jugadores[i]);

        printf("Jugador %d - %s\n", jugadores[i].id, jugadores[i].nombre);
        mostrar_mano(&(jugadores[i].mano));
        escribir_pcb(pcbs[i]);
        printf("-----------------------------------\n");
    }

    actualizar_tabla_procesos(pcbs, 4);

    printf("Juego inicializado correctamente.\n");
    return 0;
}

