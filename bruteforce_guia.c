#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>
#include <openssl/des.h>
#include <time.h>
#include <stdlib.h>

#define BLOCK_SIZE 8

void adjust_key_parity(uint64_t *key) {
    uint64_t adjusted_key = 0;
    for (int i = 0; i < 8; ++i) {
        *key <<= 1;
        adjusted_key += (*key & (0xFEULL << (i * 8)));
    }
    DES_set_odd_parity((DES_cblock *)&adjusted_key);
    *key = adjusted_key;
}

void encrypt_message(uint64_t key, unsigned char *plaintext, unsigned char *ciphertext, int length) {
    adjust_key_parity(&key);
    DES_key_schedule schedule;
    DES_set_key_unchecked((const_DES_cblock *)&key, &schedule);
    for (int i = 0; i < length; i += BLOCK_SIZE) {
        DES_ecb_encrypt(plaintext + i, ciphertext + i, &schedule, DES_ENCRYPT);
    }
}

void decrypt_message(uint64_t key, unsigned char *ciphertext, unsigned char *decryptedtext, int length) {
    adjust_key_parity(&key);
    DES_key_schedule schedule;
    DES_set_key_unchecked((const_DES_cblock *)&key, &schedule);
    for (int i = 0; i < length; i += BLOCK_SIZE) {
        DES_ecb_encrypt(ciphertext + i, decryptedtext + i, &schedule, DES_DECRYPT);
    }
}

char search[] = " the ";

int tryKey(uint64_t key, unsigned char *ciph, int len) {
    unsigned char temp[len+1];
    memcpy(temp, ciph, len);
    decrypt_message(key, temp, temp, len);
    temp[len] = 0;
    return strstr((char *)temp, search) != NULL;
}

unsigned char cipher[] = {108, 245, 65, 63, 125, 200, 150, 66, 17, 170, 207, 170, 34, 31, 70, 215, 0};

int main(int argc, char *argv[]) {
    int N, id;
    long upper = (1L << 56); // límite superior para las llaves DES 2^56
    long mylower, myupper;
    MPI_Status st;
    MPI_Request req;
    int flag;
    int ciphlen = strlen((char *)cipher);
    MPI_Comm comm = MPI_COMM_WORLD;

    MPI_Init(NULL, NULL);
    MPI_Comm_size(comm, &N);
    MPI_Comm_rank(comm, &id);

    int range_per_node = upper / N;
    mylower = range_per_node * id;
    myupper = range_per_node * (id+1) - 1;
    if(id == N-1) {
        myupper = upper;
    }

    long found = 0;
    MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

    for(int i = mylower; i < myupper && !found; ++i) {
        if(tryKey(i, cipher, ciphlen)) {
            found = i;
            for(int node = 0; node < N; node++) {
                MPI_Send(&found, 1, MPI_LONG, node, 0, MPI_COMM_WORLD);
            }
            break;
        }
    }

    if(id == 0) {
        MPI_Wait(&req, &st);
        unsigned char decrypted[ciphlen+1];
        decrypt_message(found, cipher, decrypted, ciphlen);
        decrypted[ciphlen] = 0;
        printf("%li %s\n", found, decrypted);
    }

    MPI_Finalize();

    return 0;
}
