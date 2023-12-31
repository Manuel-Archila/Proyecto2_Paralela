// Autores
// Juan Avila 20090
// Manuel Archila 161250
// Diego Franco 20240

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>
#include <openssl/des.h>
#include <time.h>
#include <stdlib.h>

#define BLOCK_SIZE 8

// Ajusta la paridad de la llave DES
// Recibe un puntero a la llave
// Modifica la llave en el lugar y no devuelve nada
void adjust_key_parity(uint64_t *key) {
    uint64_t adjusted_key = 0;
    for (int i = 0; i < 8; ++i) {
        *key <<= 1;
        adjusted_key += (*key & (0xFEULL << (i * 8)));
    }
    DES_set_odd_parity((DES_cblock *)&adjusted_key);
    *key = adjusted_key;
}

// Encripta un mensaje usando DES
// Recibe la llave, el texto plano, el texto cifrado y la longitud del texto
// Modifica el texto cifrado en el lugar y no devuelve nada
void encrypt_message(uint64_t key, unsigned char *plaintext, unsigned char *ciphertext, int length) {
    adjust_key_parity(&key);
    DES_key_schedule schedule;
    DES_set_key_unchecked((const_DES_cblock *)&key, &schedule);
    for (int i = 0; i < length; i += BLOCK_SIZE) {
        DES_ecb_encrypt(plaintext + i, ciphertext + i, &schedule, DES_ENCRYPT);
    }
}

// Desencripta un mensaje usando DES
// Recibe la llave, el texto cifrado, el texto plano y la longitud del texto
// Modifica el texto plano en el lugar y no devuelve nada
void decrypt_message(uint64_t key, unsigned char *ciphertext, unsigned char *decryptedtext, int length) {
    adjust_key_parity(&key);
    DES_key_schedule schedule;
    DES_set_key_unchecked((const_DES_cblock *)&key, &schedule);
    for (int i = 0; i < length; i += BLOCK_SIZE) {
        DES_ecb_encrypt(ciphertext + i, decryptedtext + i, &schedule, DES_DECRYPT);
    }
}

// Prueba una llave DES para ver si descifra el texto cifrado y contiene el texto de búsqueda
// Recibe la llave, el texto cifrado, la longitud del texto y el texto de búsqueda
// Devuelve 1 si la llave es correcta, 0 en caso contrario
int tryKey(uint64_t key, unsigned char *ciph, int len, char *search) {
    unsigned char temp[len+1];
    memcpy(temp, ciph, len);
    decrypt_message(key, temp, temp, len);
    temp[len] = 0;
    return strstr((char *)temp, search) != NULL;
}

// Programa principal
int main(int argc, char *argv[]) {

    // Leer archivo de texto 
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    double start_time, end_time;
    fp = fopen("entrada.txt", "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

    char *plaintext = line;
    while ((read = getline(&line, &len, fp)) != -1) {
        plaintext = line;
    }

    fclose(fp);
    printf("Texto recibido: %s\n", plaintext);

    uint64_t key = strtoull(argv[1], NULL, 10); // 10 indica base decimal
    printf("Key: %llu\n", key);


    int length = strlen(plaintext); // longitud del texto

    unsigned char ciphertext[length]; // texto cifrado
    encrypt_message(key, (unsigned char *)plaintext, ciphertext, length); // cifrar el texto
    
    int N, id;
    long upper = (1L << 56); // límite superior para las llaves DES 2^56
    long mylower, myupper;
    MPI_Status st;
    MPI_Request req;
    MPI_Comm comm = MPI_COMM_WORLD;

    MPI_Init(NULL, NULL); // Inicializar MPI
    MPI_Comm_size(comm, &N); // Número de nodos
    MPI_Comm_rank(comm, &id); // ID del nodo actual

    start_time = MPI_Wtime(); // Iniciar cronómetro
 
    long range_per_node = upper / N; // rango de llaves por nodo
    mylower = range_per_node * id; // límite inferior de llaves para el nodo actual
    myupper = range_per_node * (id+1) - 1; // límite superior de llaves para el nodo actual
    if(id == N-1) { // último nodo
        myupper = upper;
    }

    printf("Node %d: %li - %li\n", id, mylower, myupper); 
    

    // Determinar la longitud del fragmento como el 60% del plaintext (al menos 5 caracteres)
    int fragmentLength = length * 0.6;
    if (fragmentLength < 5) fragmentLength = 5;

    // Establecer una posición inicial aleatoria dentro del rango válido
    srand(time(NULL));
    int startPos = rand() % (length - fragmentLength + 1);

    char search[fragmentLength + 1];
    strncpy(search, plaintext + startPos, fragmentLength); 
    search[fragmentLength] = '\0'; // terminar la cadena
    
    long found = 0;
    MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req); // Recibir mensaje de cualquier nodo

    for(long i = mylower; i < myupper && !found; ++i) { // probar llaves
        int flag;
        MPI_Test(&req, &flag, &st); // Verificar si hemos recibido un mensaje
        if (flag) {
            break; // Si hemos recibido un mensaje, salimos del bucle
        }
        
        if(tryKey(i, ciphertext, length, search)) { // Si la llave es correcta
            found = i;
            for(int node = 0; node < N; node++) {
                if(node != id){
                    MPI_Send(&found, 1, MPI_LONG, node, 0, MPI_COMM_WORLD); // Enviar mensaje a todos los nodos
                }
            }
            break;
        }
    }


    if(id == 0) { // nodo maestro
        MPI_Wait(&req, &st); // Esperar a que todos los nodos terminen
        unsigned char decrypted[length+1]; // texto descifrado
        decrypt_message(found, ciphertext, decrypted, length); // descifrar el texto
        decrypted[length] = 0; // terminar la cadena
        printf("Key found: %li\nDecrypted text: %s\n", found, decrypted); // imprimir resultados
    }
    end_time = MPI_Wtime(); // detener cronómetro
     
    if(id == 0) { // Solo en el nodo maestro
        printf("Duration: %f seconds\n", end_time - start_time);
    }


    MPI_Finalize(); // Finalizar MPI

    return 0;
}
