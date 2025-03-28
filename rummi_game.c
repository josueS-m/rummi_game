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

#define CARTAS_INICIALES 14
#define MAX_COLOR 15
#define MAX_NOMBRE 50
#define MAX_CARTAS_GRUPO 4
#define MAX_CARTAS_ESCALERA 13
#define MAX_GRUPOS 10       // Máximo grupos en banco de apeadas
#define MAX_ESCALERAS 10    // Máximo escaleras en banco de apeadas

// ----------------------------------------------------------------------
// Estructuras y Tipos
// ----------------------------------------------------------------------

// Carta
typedef struct
{
    int numero;
    char color[MAX_COLOR];
} carta_t;

// Mano (lista de cartas del jugador)
typedef struct
{
    carta_t *cartas;
    int cantidad;
    int capacidad;
} mano_t;

typedef struct
{
    carta_t cartas[MAX_CARTAS_GRUPO];
    int cantidad;
} grupo_t;

typedef struct
{
    carta_t cartas[MAX_CARTAS_ESCALERA];
    int cantidad;
} escalera_t;


typedef struct
{
    grupo_t *grupos;       // Array dinámico de grupos
    escalera_t *escaleras; // Array dinámico de escaleras
    int total_grupos;
    int total_escaleras;
} banco_de_apeadas_t;

// Jugador
typedef struct
{
    mano_t mano;
    char nombre[MAX_NOMBRE];
    int id;
    int tiempo_restante;
    int puntos_suficientes;
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
// Funciones de Validación y Cálculo de Puntos (LOGICA)
// ----------------------------------------------------------------------

// Verifica si un conjunto de cartas forma un grupo válido
bool es_grupo_valido(const carta_t cartas[], int cantidad)
{
    if (cantidad < 3 || cantidad > MAX_CARTAS_GRUPO)
        return false;

    int numero_base = -1;
    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero != 0)
        { // Si no es comodín
            if (numero_base == -1)
            {
                numero_base = cartas[i].numero;
            }
            else if (cartas[i].numero != numero_base)
            {
                return false;
            }
        }
    }
    return (numero_base != -1); // Debe haber al menos una carta no comodín
}

// Verifica si un conjunto de cartas forma una escalera válida
bool es_escalera_valida(const carta_t cartas[], int cantidad)
{
    if (cantidad < 3 || cantidad > MAX_CARTAS_ESCALERA)
        return false;

    int numeros[MAX_CARTAS_ESCALERA];
    char color_base[MAX_COLOR] = "";
    int idx = 0;
    int comodines = 0;

    // Separar comodines y cartas normales
    for (int i = 0; i < cantidad; i++)
    {
        if (cartas[i].numero == 0)
        {
            comodines++;
        }
        else
        {
            numeros[idx] = cartas[i].numero;
            if (strlen(color_base) == 0)
            {
                strcpy(color_base, cartas[i].color);
            }
            else if (strcmp(color_base, cartas[i].color) != 0)
            {
                return false; // Diferentes colores
            }
            idx++;
        }
    }

    if (idx == 0)
        return false; // Solo comodines no es válido

    // Ordenar las cartas no comodines
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

    // Verificar si los huecos pueden ser llenados con comodines
    int huecos_necesarios = 0;
    for (int i = 0; i < idx - 1; i++)
    {
        int diferencia = numeros[i + 1] - numeros[i];
        if (diferencia <= 0 || diferencia > comodines + 1)
        {
            return false;
        }
        huecos_necesarios += (diferencia - 1);
    }

    return (huecos_necesarios <= comodines);
}

/* ---------------------- FUNCIONES DE PUNTUACIÓN ---------------------- */

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

/* ---------------------- MANEJO DE CARTAS ---------------------- */

// Remueve cartas usadas de la mano
void remover_cartas(mano_t *mano, const int indices[], int cantidad)
{
    // Ordenar índices de mayor a menor
    int indices_ordenados[cantidad];
    memcpy(indices_ordenados, indices, cantidad * sizeof(int));

    for (int i = 0; i < cantidad - 1; i++)
    {
        for (int j = i + 1; j < cantidad; j++)
        {
            if (indices_ordenados[i] < indices_ordenados[j])
            {
                int temp = indices_ordenados[i];
                indices_ordenados[i] = indices_ordenados[j];
                indices_ordenados[j] = temp;
            }
        }
    }

    // Eliminar cartas
    for (int i = 0; i < cantidad; i++)
    {
        for (int j = indices_ordenados[i]; j < mano->cantidad - 1; j++)
        {
            mano->cartas[j] = mano->cartas[j + 1];
        }
        mano->cantidad--;
    }
}

/* ---------------------- BÚSQUEDA DE COMBINACIONES ---------------------- */

// Busca el mejor grupo disponible en la mano y lo añade al banco
bool buscar_mejor_grupo(mano_t *mano, banco_de_apeadas_t *banco, int *puntos)
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
        // Añadir el mejor grupo encontrado al banco
        if (banco->total_grupos >= MAX_GRUPOS)
        {
            fprintf(stderr, "Error: Capacidad máxima de grupos alcanzada\n");
            return false;
        }

        grupo_t *nuevo_grupo = &banco->grupos[banco->total_grupos++];
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
bool buscar_mejor_escalera(mano_t *mano, banco_de_apeadas_t *banco, int *puntos)
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
        // Añadir la mejor escalera encontrada al banco
        if (banco->total_escaleras >= MAX_ESCALERAS)
        {
            fprintf(stderr, "Error: Capacidad máxima de escaleras alcanzada\n");
            return false;
        }

        escalera_t *nueva_escalera = &banco->escaleras[banco->total_escaleras++];
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
void buscar_combinaciones(mano_t *mano, banco_de_apeadas_t *banco, int *puntos, bool priorizar_grupos)
{
    bool seguir_buscando = true;
    while (seguir_buscando)
    {
        bool encontrado = priorizar_grupos ? buscar_mejor_grupo(mano, banco, puntos) || buscar_mejor_escalera(mano, banco, puntos) : buscar_mejor_escalera(mano, banco, puntos) || buscar_mejor_grupo(mano, banco, puntos);

        seguir_buscando = encontrado;
    }
}

// Busca combinación mixta óptima y la añade al banco
void buscar_combinacion_mixta(mano_t *mano, banco_de_apeadas_t *banco, int *puntos)
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
            buscar_mejor_grupo(mano, banco, puntos);
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
                buscar_mejor_escalera(mano, banco, puntos);
            }
        }
    }
}

/* ---------------------- FUNCIÓN PRINCIPAL PARA CREAR APEADA ---------------------- */

banco_de_apeadas_t crear_mejor_apeada(jugador_t *jugador)
{
    banco_de_apeadas_t mejor_banco;
    banco_inicializar(&mejor_banco);

    int max_puntos = 0;

    // Probar diferentes estrategias
    for (int estrategia = 0; estrategia < 3; estrategia++)
    {
        // Crear una copia temporal de la mano del jugador
        mano_t mano_temp;
        mano_inicializar(&mano_temp, jugador->mano.capacidad);
        memcpy(mano_temp.cartas, jugador->mano.cartas, jugador->mano.cantidad * sizeof(carta_t));
        mano_temp.cantidad = jugador->mano.cantidad;

        banco_de_apeadas_t banco_temp;
        banco_inicializar(&banco_temp);
        int puntos_temp = 0;

        switch (estrategia)
        {
        case 0:
            buscar_combinaciones(&mano_temp, &banco_temp, &puntos_temp, true);
            break;
        case 1:
            buscar_combinaciones(&mano_temp, &banco_temp, &puntos_temp, false);
            break;
        case 2:
            buscar_combinacion_mixta(&mano_temp, &banco_temp, &puntos_temp);
            break;
        }

        if (puntos_temp > max_puntos)
        {
            max_puntos = puntos_temp;
            // Liberar el anterior mejor banco
            banco_liberar(&mejor_banco);
            // Asignar el nuevo
            mejor_banco = banco_temp;
            // Ahora necesitamos actualizar la mano del jugador
            memcpy(jugador->mano.cartas, mano_temp.cartas, mano_temp.cantidad * sizeof(carta_t));
            jugador->mano.cantidad = mano_temp.cantidad;
        }
        else
        {
            banco_liberar(&banco_temp);
        }

        mano_liberar(&mano_temp);
    }

    return mejor_banco;
}

/* ---------------------- FUNCIONES PARA EMBONAR ---------------------- */

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

// Función principal para embonar una carta del jugador al banco
bool embonar_carta(jugador_t *jugador, banco_de_apeadas_t *banco, int indice_carta)
{
    if (indice_carta < 0 || indice_carta >= jugador->mano.cantidad)
    {
        return false;
    }

    carta_t carta = jugador->mano.cartas[indice_carta];
    bool embonada = false;

    // Primero intentar embonar en grupos existentes
    for (int i = 0; i < banco->total_grupos; i++)
    {
        if (puede_embonar_grupo(&carta, &banco->grupos[i]))
        {
            // Agregar carta al grupo
            banco->grupos[i].cartas[banco->grupos[i].cantidad++] = carta;
            // Eliminar carta de la mano
            remover_cartas(&jugador->mano, &indice_carta, 1);
            embonada = true;
            break;
        }
    }

    // Si no se pudo embonar en grupos, intentar con escaleras
    if (!embonada)
    {
        for (int i = 0; i < banco->total_escaleras; i++)
        {
            if (puede_embonar_escalera(&carta, &banco->escaleras[i]))
            {
                // Agregar carta a la escalera
                banco->escaleras[i].cartas[banco->escaleras[i].cantidad++] = carta;
                // Eliminar carta de la mano
                remover_cartas(&jugador->mano, &indice_carta, 1);
                embonada = true;
                break;
            }
        }
    }

    // Si aún no se pudo embonar, intentar mover comodines
    if (!embonada)
    {
        embonada = mover_comodin_para_embonar(banco, &carta);
        if (embonada)
        {
            // Si se movió un comodín, ahora agregar la carta
            for (int i = 0; i < banco->total_grupos; i++)
            {
                if (puede_embonar_grupo(&carta, &banco->grupos[i]))
                {
                    banco->grupos[i].cartas[banco->grupos[i].cantidad++] = carta;
                    remover_cartas(&jugador->mano, &indice_carta, 1);
                    break;
                }
            }
            for (int i = 0; i < banco->total_escaleras; i++)
            {
                if (puede_embonar_escalera(&carta, &banco->escaleras[i]))
                {
                    banco->escaleras[i].cartas[banco->escaleras[i].cantidad++] = carta;
                    remover_cartas(&jugador->mano, &indice_carta, 1);
                    break;
                }
            }
        }
    }

    return embonada;
}

// Función para que el jugador intente embonar todas las cartas posibles
void jugador_embonar_cartas(jugador_t *jugador, banco_de_apeadas_t *banco)
{
    bool seguir_embonando = true;
    while (seguir_embonando)
    {
        bool embonada_algo = false;

        // Intentar embonar cada carta (empezando por la última para evitar problemas con índices)
        for (int i = jugador->mano.cantidad - 1; i >= 0; i--)
        {
            if (embonar_carta(jugador, banco, i))
            {
                embonada_algo = true;
                // Reiniciar el bucle porque los índices han cambiado
                break;
            }
        }

        seguir_embonando = embonada_algo;
    }
}

/* ---------------------- FUNCIONES PARA DETERMINAR GANADOR ---------------------- */

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