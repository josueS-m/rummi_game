#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <pthread.h>

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *format, ...) {
    pthread_mutex_lock(&log_mutex);
    FILE *log_file = fopen("log.txt", "a");
    if (!log_file) return;
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    vprintf(format, args); 		//Imprime en consola tambien
    va_end(args);
    
    fprintf(log_file, "\n");
    fclose(log_file);
    pthread_mutex_unlock(&log_mutex);
}
