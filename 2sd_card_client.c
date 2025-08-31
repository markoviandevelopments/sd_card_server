#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define SERVER_IP "192.168.1.250"
#define SERVER_PORT 8023
#define START_ADDR 20480000

#define WRITE_MESSAGE 0
#define READ_MESSAGE 1

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        /* out of memory! */
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
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);

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

int write_message(long long start_addr, const char *message) {
    size_t len = strlen(message);
    for (size_t i = 0; i < len; i++) {
        if (write_byte(start_addr + i, (unsigned char)message[i]) != 0) {
            return -1;
        }
    }
    // Write null terminator
    if (write_byte(start_addr + len, 0) != 0) {
        return -1;
    }
    return 0;
}

char *read_message(long long start_addr) {
    char *message = malloc(1024); // Assume max 1023 chars
    if (!message) return NULL;
    unsigned char byte;
    size_t i = 0;
    while (1) {
        if (read_byte(start_addr + i, &byte) != 0) {
            free(message);
            return NULL;
        }
        message[i] = (char)byte;
        if (byte == 0) break;
        i++;
        if (i >= 1023) {
            message[i] = 0;
            break;
        }
    }
    return message;
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    long long addr = START_ADDR;

    if (WRITE_MESSAGE) {
        const char *message = "Hello, World!";
        

        printf("Writing message \"%s\" starting at address %lld\n", message, addr);
        if (write_message(addr, message) == 0) {
            printf("Write successful.\n");
        } else {
            printf("Write failed.\n");
            curl_global_cleanup();
            return 1;
        }
    }

    if (READ_MESSAGE) {
        printf("Reading message from address %lld\n", addr);
        char *read_msg = read_message(addr);
        if (read_msg) {
            printf("Read message: \"%s\"\n", read_msg);
            free(read_msg);
        } else {
            printf("Read failed.\n");
        }
    }

    curl_global_cleanup();
    return 0;
}
