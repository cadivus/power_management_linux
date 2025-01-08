#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 4096  // Größe der Matrix (groß für Cache-Miss)
#define M 4096

int matrix[N][M];

void initialize_matrix() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            matrix[i][j] = rand() % 100;
        }
    }
}

long long sum_by_columns() {
    long long sum = 0;
    for (int j = 0; j < M; j++) {
        for (int i = 0; i < N; i++) {
            sum += matrix[i][j];  // Zugriff spaltenweise (Cache-Miss provoziert)
        }
    }
    return sum;
}

int main() {
    srand(time(NULL));
    initialize_matrix();

    clock_t start = clock();
    long long sum = sum_by_columns();
    clock_t end = clock();

    printf("Sum: %lld\n", sum);
    printf("Time: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    return 0;
}

