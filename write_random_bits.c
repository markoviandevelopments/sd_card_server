#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>


#define SERVER_IP "192.168.1.250" // 250 and 212
#define SERVER_PORT 8023 // 8023 and 8025
#define START_ADDR 2048

#define NUM_BITS 8

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}


int write_byte(long long addr, unsigned char value) {
    CURL *curl;
    CURLcode res;
    char url[256];

    snprintf(url, sizeof(url), "http://%s:%d/write?addr=%lld&value=%d", SERVER_IP, SERVER_PORT, addr, value);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // Default to stdout if needed, but we check HTTP code
        res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);
        printf("\n");

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return -1;
        }

        if (http_code != 200) {
            fprintf(stderr, "Server returned HTTP %ld\n", http_code);
            return -1;
        }

        return 0;
    }
    return -1;
}

int read_byte(long long addr, unsigned char *value) {
    CURL *curl;
    CURLcode res;
    char url[256];
    struct MemoryStruct chunk;

    chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    chunk.size = 0;    /* no data at this point */

    snprintf(url, sizeof(url), "http://%s:%d/read?addr=%lld", SERVER_IP, SERVER_PORT, addr);
    

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);
        printf("\n");

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            return -1;
        }

        if (http_code != 200) {
            fprintf(stderr, "Server returned HTTP %ld\n", http_code);
            free(chunk.memory);
            return -1;
        }

        *value = (unsigned char)atoi(chunk.memory);
        free(chunk.memory);
        return 0;
    }
    free(chunk.memory);
    return -1;
}

char pull_random_byte() {
    int a = 0;
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, "http://192.168.1.238:8003/bits?count=8"); //alt 108.254.1.184
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            // Find the start of the bits array
            char *array_start = strchr(chunk.memory, '[');
            if (array_start) {
                array_start++; // Skip '['
                int bits[NUM_BITS];
                for (int i = 0; i < NUM_BITS; i++) {
                    bits[i] = array_start[2 * i] - '0';
                }
                // Now bits[] contains the decoded buffer; print it as an example
                printf("Decoded bits: ");
                for (int i = 0; i < NUM_BITS; i++) {
                    printf("%d ", bits[i]);
                }
                printf("\n");

                
                int *ptr = &a;

                for (int i=0; i<8;i++){
                    if (bits[i] == 1) {
                        *ptr |= (1 << i);
                    }
                }


                printf("Random byte: %d\n", a);



            } else {
                printf("Failed to find bits array in response.\n");
            }
        }
        curl_easy_cleanup(curl_handle);
    }
    free(chunk.memory);
    curl_global_cleanup();
    return (char) a;
}

int main(void) {
    unsigned char a;
    unsigned int b;
    long long addr_offset = 0;
    for (long long i=0;i<1000000;i++) {
        a = pull_random_byte();
        if (addr_offset < 150) {
            if (i % 6 > 0) {
                a = 50;
            } else {
                a = 50;
            }
        } else if (addr_offset < 500){
            if (a > 180) {
                a = 50;
            } else {
                a = 0;
            }
        } else {
            if (a > 100) {
                a = 50;
            } else {
                a = 0;
            }
        }
        b = (unsigned int) a;
        write_byte(START_ADDR + addr_offset, a);
        printf("Wrote %u to Address: %lld\n", a, START_ADDR + addr_offset);

        printf("\n\nVERIFICATION\n\n");
        printf("Address offset: %lld\n", addr_offset);
        usleep(500000);

        read_byte(START_ADDR + addr_offset, &a);
        b = (unsigned int) a;
        printf("Read :%u from Address: %lld\n", b, START_ADDR + addr_offset);
        printf("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        usleep(500000);
        addr_offset++;
        addr_offset = addr_offset % 900;
    }

    return 0;
}
