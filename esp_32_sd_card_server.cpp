#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#define CS 5
#define MOSI 23
#define MISO 19
#define SCK 18
#define BLOCK_SIZE 512

const char* ssid = "BrubakerWifi"; // Replace with your WiFi SSID
const char* password = "Pre$ton01"; // Replace with your WiFi password

WebServer server(8023); // 8023 or 8025

int is_high_capacity = 0;
static uint8_t block_buffer[BLOCK_SIZE];

// Bit-bang SPI transfer
uint8_t spi_transfer(uint8_t data, int slow) {
    uint8_t recv = 0;
    int delay_us = slow ? 5 : 1;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(MOSI, (data & (1 << i)) ? 1 : 0);
        delayMicroseconds(delay_us);
        digitalWrite(SCK, 1);
        delayMicroseconds(delay_us);
        if (digitalRead(MISO)) recv |= (1 << i);
        digitalWrite(SCK, 0);
        delayMicroseconds(delay_us);
    }
    return recv;
}

// Send non-data command (CS high after response)
uint8_t send_command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t* extra_resp, int extra_bytes) {
    uint8_t response = 0xFF;
    uint8_t resp_bytes[128]; // Increased buffer
    int r_idx = 0;
    digitalWrite(CS, LOW);
    spi_transfer(cmd, 0);
    spi_transfer((arg >> 24) & 0xFF, 0);
    spi_transfer((arg >> 16) & 0xFF, 0);
    spi_transfer((arg >> 8) & 0xFF, 0);
    spi_transfer(arg & 0xFF, 0);
    spi_transfer(crc, 0);
    // Read initial response (R1) - increased loop
    for (int i = 0; i < 64; i++) {
        resp_bytes[r_idx] = spi_transfer(0xFF, 0);
        if (resp_bytes[r_idx] != 0xFF) {
            response = resp_bytes[r_idx];
            r_idx++;
            break;
        }
        r_idx++;
    }
    // Read extra response if requested
    if (extra_resp && extra_bytes > 0) {
        for (int i = 0; i < extra_bytes; i++) {
            extra_resp[i] = spi_transfer(0xFF, 0);
        }
    }
    digitalWrite(CS, HIGH);
    // Extra clocks
    for (int i = 0; i < 8; i++) {
        spi_transfer(0xFF, 0);
    }
    delayMicroseconds(100);
    Serial.print("Command 0x");
    Serial.print(cmd, HEX);
    Serial.print(" response bytes: ");
    for (int i = 0; i < r_idx; i++) {
        Serial.print("0x");
        Serial.print(resp_bytes[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    return response;
}

// Read single block (keep CS low until done)
int read_single_block(uint32_t addr, uint8_t* buffer) {
    uint8_t resp = 0xFF;
    uint8_t token = 0xFF;
    digitalWrite(CS, LOW);
    // Send CMD17
    spi_transfer(0x51, 1); // Slow for data
    spi_transfer((addr >> 24) & 0xFF, 1);
    spi_transfer((addr >> 16) & 0xFF, 1);
    spi_transfer((addr >> 8) & 0xFF, 1);
    spi_transfer(addr & 0xFF, 1);
    spi_transfer(0xFF, 1); // Dummy CRC
    // Get R1 - increased loop
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD17 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -1;
    }
    // Wait for token - increased timeout
    for (int i = 0; i < 10000; i++) {
        token = spi_transfer(0xFF, 1);
        if (token != 0xFF) break;
    }
    Serial.print("Data token: 0x");
    Serial.println(token, HEX);
    if (token != 0xFE) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -2;
    }
    // Read data
    for (int i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = spi_transfer(0xFF, 1);
    }
    // Discard CRC
    spi_transfer(0xFF, 1);
    spi_transfer(0xFF, 1);
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1); // Extra release
    return 0;
}

// Write single block (keep CS low until done)
int write_single_block(uint32_t addr, const uint8_t* buffer) {
    uint8_t resp = 0xFF;
    uint8_t token = 0xFE; // For single block
    digitalWrite(CS, LOW);
    // Send CMD24
    spi_transfer(0x58, 1);
    spi_transfer((addr >> 24) & 0xFF, 1);
    spi_transfer((addr >> 16) & 0xFF, 1);
    spi_transfer((addr >> 8) & 0xFF, 1);
    spi_transfer(addr & 0xFF, 1);
    spi_transfer(0xFF, 1); // Dummy CRC
    // Get R1 - increased
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD24 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -1;
    }
    // Send token and data
    spi_transfer(token, 1);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        spi_transfer(buffer[i], 1);
    }
    spi_transfer(0xFF, 1); // Dummy CRC
    spi_transfer(0xFF, 1);
    // Data response - added loop
    uint8_t data_resp = 0xFF;
    for (int i = 0; i < 64; i++) {
        data_resp = spi_transfer(0xFF, 1);
        if (data_resp != 0xFF) break;
    }
    data_resp &= 0x1F;
    Serial.print("Data response: 0x");
    Serial.println(data_resp, HEX);
    if (data_resp != 0x05) { // Accepted
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -2;
    }
    // Busy wait - increased timeout
    uint8_t busy = 0x00;
    int busy_count = 0;
    for (int i = 0; i < 10000000; i++) { // 10M
        busy = spi_transfer(0xFF, 1);
        busy_count++;
        if (busy != 0x00) break;
    }
    Serial.print("Busy end after ");
    Serial.print(busy_count);
    Serial.print(" cycles: 0x");
    Serial.println(busy, HEX);
    if (busy == 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -3;
    }
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    return 0;
}

// Function to write a single byte at a byte address
int write_byte(uint64_t byte_addr, uint8_t value) {
    uint32_t block_number = byte_addr / BLOCK_SIZE;
    uint32_t offset = byte_addr % BLOCK_SIZE;
    uint32_t block_addr = block_number * (is_high_capacity ? 1u : BLOCK_SIZE);
    if (read_single_block(block_addr, block_buffer) != 0) {
        return -1;
    }
    block_buffer[offset] = value;
    if (write_single_block(block_addr, block_buffer) != 0) {
        return -2;
    }
    return 0;
}

// Function to read a single byte at a byte address
uint8_t read_byte(uint64_t byte_addr) {
    uint32_t block_number = byte_addr / BLOCK_SIZE;
    uint32_t offset = byte_addr % BLOCK_SIZE;
    uint32_t block_addr = block_number * (is_high_capacity ? 1u : BLOCK_SIZE);
    if (read_single_block(block_addr, block_buffer) != 0) {
        return 0; // Error, return 0
    }
    return block_buffer[offset];
}

// Read multiple blocks (CMD18)
int read_multiple_blocks(uint32_t addr, uint8_t* buffer, int num_blocks) {
    uint8_t resp = 0xFF;
    digitalWrite(CS, LOW);
    // Send CMD18
    spi_transfer(0x52, 1);
    spi_transfer((addr >> 24) & 0xFF, 1);
    spi_transfer((addr >> 16) & 0xFF, 1);
    spi_transfer((addr >> 8) & 0xFF, 1);
    spi_transfer(addr & 0xFF, 1);
    spi_transfer(0xFF, 1); // Dummy CRC
    // Get R1 - increased
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD18 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -1;
    }
    for (int block = 0; block < num_blocks; block++) {
        uint8_t token = 0xFF;
        for (int i = 0; i < 10000; i++) { // Increased
            token = spi_transfer(0xFF, 1);
            if (token != 0xFF) break;
        }
        Serial.print("Block ");
        Serial.print(block);
        Serial.print(" data token: 0x");
        Serial.println(token, HEX);
        if (token != 0xFE) {
            send_command(0x4C, 0x00000000, 0xFF, NULL, 0); // CMD12 to stop
            digitalWrite(CS, HIGH);
            for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
            return -2;
        }
        // Read data
        for (int i = 0; i < BLOCK_SIZE; i++) {
            buffer[block * BLOCK_SIZE + i] = spi_transfer(0xFF, 1);
        }
        // Discard CRC
        spi_transfer(0xFF, 1);
        spi_transfer(0xFF, 1);
    }
    // Send CMD12 to stop transmission
    resp = send_command(0x4C, 0x00000000, 0xFF, NULL, 0);
    Serial.print("CMD12 R1: 0x");
    Serial.println(resp, HEX);
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    return 0;
}

// Write multiple blocks (CMD25)
int write_multiple_blocks(uint32_t addr, const uint8_t* buffer, int num_blocks) {
    uint8_t resp = 0xFF;
    digitalWrite(CS, LOW);
    // Send CMD25
    spi_transfer(0x59, 1);
    spi_transfer((addr >> 24) & 0xFF, 1);
    spi_transfer((addr >> 16) & 0xFF, 1);
    spi_transfer((addr >> 8) & 0xFF, 1);
    spi_transfer(addr & 0xFF, 1);
    spi_transfer(0xFF, 1); // Dummy CRC
    // Get R1
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD25 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
        return -1;
    }
    for (int block = 0; block < num_blocks; block++) {
        // Send data token for multi block
        spi_transfer(0xFC, 1);
        // Send data
        for (int i = 0; i < BLOCK_SIZE; i++) {
            spi_transfer(buffer[block * BLOCK_SIZE + i], 1);
        }
        // Dummy CRC
        spi_transfer(0xFF, 1);
        spi_transfer(0xFF, 1);
        // Data response
        uint8_t data_resp = 0xFF;
        for (int i = 0; i < 64; i++) {
            data_resp = spi_transfer(0xFF, 1);
            if (data_resp != 0xFF) break;
        }
        data_resp &= 0x1F;
        Serial.print("Block ");
        Serial.print(block);
        Serial.print(" data response: 0x");
        Serial.println(data_resp, HEX);
        if (data_resp != 0x05) {
            digitalWrite(CS, HIGH);
            for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
            return -2;
        }
        // Busy wait - increased
        uint8_t busy = 0x00;
        for (int i = 0; i < 10000000; i++) {
            busy = spi_transfer(0xFF, 1);
            if (busy != 0x00) break;
        }
        if (busy == 0x00) {
            digitalWrite(CS, HIGH);
            for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
            return -3;
        }
    }
    // Send stop token
    spi_transfer(0xFD, 1);
    // Busy wait after stop - increased
    uint8_t busy = 0x00;
    for (int i = 0; i < 10000000; i++) {
        busy = spi_transfer(0xFF, 1);
        if (busy != 0x00) break;
    }
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    return 0;
}

// Function to erase blocks (CMD32 + CMD33 + CMD38)
int erase_blocks(uint32_t start_addr, uint32_t end_addr) {
    uint8_t resp;
    // Set erase start
    resp = send_command(0x60, start_addr, 0xFF, NULL, 0);
    if (resp != 0x00) {
        Serial.print("CMD32 failed: 0x");
        Serial.println(resp, HEX);
        return -1;
    }
    // Set erase end
    resp = send_command(0x61, end_addr, 0xFF, NULL, 0);
    if (resp != 0x00) {
        Serial.print("CMD33 failed: 0x");
        Serial.println(resp, HEX);
        return -2;
    }
    // Erase
    resp = send_command(0x66, 0x00000000, 0xFF, NULL, 0);
    if (resp != 0x00) {
        Serial.print("CMD38 failed: 0x");
        Serial.println(resp, HEX);
        return -3;
    }
    // Busy wait - increased
    digitalWrite(CS, LOW);
    uint8_t busy = 0x00;
    for (int i = 0; i < 100000000; i++) { // 100M for erase
        busy = spi_transfer(0xFF, 1);
        if (busy != 0x00) break;
    }
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    if (busy == 0x00) {
        return -4;
    }
    Serial.println("Erase successful.");
    return 0;
}

// Function to get card capacity from CSD
uint64_t get_card_capacity(uint8_t* csd) {
    uint8_t csd_structure = (csd[0] >> 6) & 0x03;
    if (csd_structure == 0) { // Standard capacity
        uint32_t c_size = ((csd[6] & 0x03) << 10) | (csd[7] << 2) | ((csd[8] >> 6) & 0x03);
        uint8_t c_size_mult = ((csd[9] & 0x03) << 1) | ((csd[10] >> 7) & 0x01);
        uint8_t read_bl_len = csd[5] & 0x0F;
        uint64_t capacity = (uint64_t)(c_size + 1) << (c_size_mult + 2);
        capacity <<= (read_bl_len - 9); // In 512-byte sectors, then to MB: / 2
        return capacity / 2;
    } else if (csd_structure == 1) { // High capacity
        uint32_t c_size = ((csd[7] & 0x3F) << 16) | (csd[8] << 8) | csd[9];
        // Capacity in MB: (c_size + 1) * 512 KB / 1024 KB/MB = (c_size + 1) / 2
        return ((uint64_t)(c_size + 1) * 512ULL) / 1024;
    } else {
        return 0;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    pinMode(CS, OUTPUT); digitalWrite(CS, HIGH);
    pinMode(MOSI, OUTPUT); digitalWrite(MOSI, 1);
    pinMode(SCK, OUTPUT); digitalWrite(SCK, 0);
    pinMode(MISO, INPUT_PULLUP);

    delay(10);
    Serial.println("Sending extended power-on clocks...");
    for (int i = 0; i < 80; i++) { // Increased
        spi_transfer(0xFF, 0);
    }
    Serial.println("Sending CMD0 (with retries)...");
    uint8_t resp;
    int cmd0_retries = 20;
    while (cmd0_retries--) {
        resp = send_command(0x40, 0x00000000, 0x95, NULL, 0);
        if (resp == 0x01) break;
        delay(100);
    }
    if (resp != 0x01) {
        Serial.println("CMD0 failed after retries.");
        while (true) delay(1000); // Loop instead of return
    }
    Serial.println("Sending CMD8...");
    uint8_t cmd8_extra[4];
    resp = send_command(0x48, 0x000001AA, 0x87, cmd8_extra, 4);
    uint32_t acmd41_arg = 0x00000000;
    is_high_capacity = 0;
    if (resp == 0x01 && cmd8_extra[2] == 0x01 && cmd8_extra[3] == 0xAA) {
        acmd41_arg = 0x40000000;
        is_high_capacity = 1;
        Serial.println("Detected Ver2+ card.");
    } else if (resp == 0x05) {
        Serial.println("Detected Ver1 card.");
    } else {
        Serial.print("CMD8 unexpected: 0x");
        Serial.print(resp, HEX);
        Serial.println(" - Falling back.");
    }
    Serial.println("Initializing card...");
    int retries = 1000;
    while (retries--) {
        if (acmd41_arg == 0x00000000) {
            resp = send_command(0x41, 0x00000000, 0xFF, NULL, 0);
        } else {
            resp = send_command(0x77, 0x00000000, 0x65, NULL, 0);
            if (resp > 0x01) continue;
            resp = send_command(0x69, acmd41_arg, 0x77, NULL, 0);
        }
        if (resp == 0x00) break;
        delay(1);
    }
    if (resp != 0x00) {
        Serial.print("Initialization timeout: Last response 0x");
        Serial.println(resp, HEX);
        while (true) delay(1000);
    }
    Serial.println("SD Card initialized successfully!");
    Serial.println("Sending CMD58...");
    uint8_t ocr[4];
    resp = send_command(0x7A, 0x00000000, 0xFF, ocr, 4);
    if (resp == 0x00) {
        Serial.print("OCR: 0x");
        Serial.print(ocr[0], HEX);
        Serial.print(" 0x");
        Serial.print(ocr[1], HEX);
        Serial.print(" 0x");
        Serial.print(ocr[2], HEX);
        Serial.print(" 0x");
        Serial.println(ocr[3], HEX);
        if (ocr[0] & 0x40) Serial.println("High capacity card (SDHC/SDXC).");
    }
    // Fetch CID (CMD10)
    Serial.println("Fetching CID (CMD10)...");
    uint8_t cid[16];
    digitalWrite(CS, LOW);
    spi_transfer(0x4A, 1); // CMD10
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0xFF, 1);
    // Get R1 - increased
    resp = 0xFF;
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD10 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    } else {
        // Wait for token
        uint8_t token = 0xFF;
        for (int i = 0; i < 10000; i++) {
            token = spi_transfer(0xFF, 1);
            if (token != 0xFF) break;
        }
        if (token == 0xFE) {
            for (int i = 0; i < 16; i++) {
                cid[i] = spi_transfer(0xFF, 1);
            }
            // Discard CRC
            spi_transfer(0xFF, 1);
            spi_transfer(0xFF, 1);
            Serial.print("CID: ");
            for (int i = 0; i < 16; i++) {
                Serial.print("0x");
                Serial.print(cid[i], HEX);
                Serial.print(" ");
            }
            Serial.println();
        } else {
            Serial.print("Invalid CID token: 0x");
            Serial.println(token, HEX);
        }
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    }
    // Fetch CSD (CMD9)
    Serial.println("Fetching CSD (CMD9)...");
    uint8_t csd[16];
    digitalWrite(CS, LOW);
    spi_transfer(0x49, 1); // CMD9
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0x00, 1);
    spi_transfer(0xFF, 1);
    // Get R1
    resp = 0xFF;
    for (int i = 0; i < 64; i++) {
        resp = spi_transfer(0xFF, 1);
        if (resp != 0xFF) break;
    }
    Serial.print("CMD9 R1: 0x");
    Serial.println(resp, HEX);
    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    } else {
        // Wait for token
        uint8_t token = 0xFF;
        for (int i = 0; i < 10000; i++) {
            token = spi_transfer(0xFF, 1);
            if (token != 0xFF) break;
        }
        if (token == 0xFE) {
            for (int i = 0; i < 16; i++) {
                csd[i] = spi_transfer(0xFF, 1);
            }
            // Discard CRC
            spi_transfer(0xFF, 1);
            spi_transfer(0xFF, 1);
            Serial.print("CSD: ");
            for (int i = 0; i < 16; i++) {
                Serial.print("0x");
                Serial.print(csd[i], HEX);
                Serial.print(" ");
            }
            Serial.println();
            uint64_t capacity_mb = get_card_capacity(csd);
            Serial.print("Card capacity: ");
            Serial.print(capacity_mb);
            Serial.println(" MB");
        } else {
            Serial.print("Invalid CSD token: 0x");
            Serial.println(token, HEX);
        }
        digitalWrite(CS, HIGH);
        for (int i = 0; i < 8; i++) spi_transfer(0xFF, 1);
    }
    // Test single block read/write at block 0
    uint8_t buffer[BLOCK_SIZE * 10]; // Buffer for multi
    memset(buffer, 0, sizeof(buffer));
    uint32_t block_0_addr = 0 * (is_high_capacity ? 1 : BLOCK_SIZE);
    Serial.print("Attempting single block read at address ");
    Serial.print(block_0_addr);
    Serial.println(" (CMD17)...");
    if (read_single_block(block_0_addr, buffer) == 0) {
        Serial.print("First 16 bytes at address 0: ");
        for (int i = 0; i < 16; i++) {
            Serial.print("0x");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    } else {
        Serial.println("Read failed at address 0.");
    }
    // Prepare pattern for write: incremental bytes
    for (int i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = (uint8_t)i;
    }
    Serial.print("Attempting single block write at address ");
    Serial.print(block_0_addr);
    Serial.println(" (CMD24)...");
    if (write_single_block(block_0_addr, buffer) == 0) {
        Serial.println("Write successful at address 0.");
    } else {
        Serial.println("Write failed at address 0.");
    }
    delay(10); // Extra delay after write
    // Verify
    memset(buffer, 0, BLOCK_SIZE);
    if (read_single_block(block_0_addr, buffer) == 0) {
        int verify_ok = 1;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            if (buffer[i] != (uint8_t)i) {
                verify_ok = 0;
                break;
            }
        }
        Serial.print("Verify at address 0: ");
        Serial.println(verify_ok ? "OK" : "Failed");
    } else {
        Serial.println("Verify read failed at address 0.");
    }
    // Test at arbitrary address, e.g., block 100
    uint32_t arbitrary_block = 100;
    uint32_t arbitrary_addr = arbitrary_block * (is_high_capacity ? 1 : BLOCK_SIZE);
    uint64_t arbitrary_byte_offset = (uint64_t)arbitrary_block * BLOCK_SIZE;
    Serial.print("Attempting single block read at arbitrary address ");
    Serial.print(arbitrary_addr);
    Serial.print(" (byte offset ");
    Serial.print(arbitrary_byte_offset);
    Serial.println(")...");
    memset(buffer, 0, BLOCK_SIZE);
    if (read_single_block(arbitrary_addr, buffer) == 0) {
        Serial.print("First 16 bytes at arbitrary address: ");
        for (int i = 0; i < 16; i++) {
            Serial.print("0x");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    } else {
        Serial.println("Read failed at arbitrary address.");
    }
    // Write pattern
    for (int i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = (uint8_t)(255 - i); // Inverted pattern
    }
    Serial.print("Attempting single block write at arbitrary address ");
    Serial.print(arbitrary_addr);
    Serial.println("...");
    if (write_single_block(arbitrary_addr, buffer) == 0) {
        Serial.println("Write successful at arbitrary address.");
    } else {
        Serial.println("Write failed at arbitrary address.");
    }
    delay(10);
    // Verify
    memset(buffer, 0, BLOCK_SIZE);
    if (read_single_block(arbitrary_addr, buffer) == 0) {
        int verify_ok = 1;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            if (buffer[i] != (uint8_t)(255 - i)) {
                verify_ok = 0;
                break;
            }
        }
        Serial.print("Verify at arbitrary address: ");
        Serial.println(verify_ok ? "OK" : "Failed");
    } else {
        Serial.println("Verify read failed at arbitrary address.");
    }
    // Test multi-block read/write
    int num_blocks = 5;
    uint32_t multi_block = 200;
    uint32_t multi_addr = multi_block * (is_high_capacity ? 1 : BLOCK_SIZE);
    uint64_t multi_byte_offset = (uint64_t)multi_block * BLOCK_SIZE;
    // Prepare multi pattern
    for (int b = 0; b < num_blocks; b++) {
        for (int i = 0; i < BLOCK_SIZE; i++) {
            buffer[b * BLOCK_SIZE + i] = (uint8_t)(b + i % 256);
        }
    }
    Serial.print("Attempting multi-block write (");
    Serial.print(num_blocks);
    Serial.print(" blocks) at address ");
    Serial.print(multi_addr);
    Serial.print(" (byte offset ");
    Serial.print(multi_byte_offset);
    Serial.println(") (CMD25)...");
    if (write_multiple_blocks(multi_addr, buffer, num_blocks) == 0) {
        Serial.println("Multi-block write successful.");
    } else {
        Serial.println("Multi-block write failed.");
    }
    delay(50); // Longer delay after multi-write
    // Verify multi read
    memset(buffer, 0, BLOCK_SIZE * num_blocks);
    Serial.print("Attempting multi-block read (");
    Serial.print(num_blocks);
    Serial.print(" blocks) at address ");
    Serial.print(multi_addr);
    Serial.println(" (CMD18)...");
    if (read_multiple_blocks(multi_addr, buffer, num_blocks) == 0) {
        int verify_ok = 1;
        for (int b = 0; b < num_blocks; b++) {
            for (int i = 0; i < BLOCK_SIZE; i++) {
                if (buffer[b * BLOCK_SIZE + i] != (uint8_t)(b + i % 256)) {
                    verify_ok = 0;
                    break;
                }
            }
            if (!verify_ok) break;
        }
        Serial.print("Multi-block verify: ");
        Serial.println(verify_ok ? "OK" : "Failed");
    } else {
        Serial.println("Multi-block read failed.");
    }
    // Test erase
    uint32_t erase_block_start = 300;
    int erase_num = 2;
    uint32_t erase_start = erase_block_start * (is_high_capacity ? 1 : BLOCK_SIZE);
    uint32_t erase_end = (erase_block_start + erase_num - 1) * (is_high_capacity ? 1 : BLOCK_SIZE);
    uint64_t erase_byte_start = (uint64_t)erase_block_start * BLOCK_SIZE;
    uint64_t erase_byte_end = erase_byte_start + (erase_num * BLOCK_SIZE) - 1;
    Serial.print("Attempting to erase blocks from ");
    Serial.print(erase_start);
    Serial.print(" to ");
    Serial.print(erase_end);
    Serial.print(" (byte ");
    Serial.print(erase_byte_start);
    Serial.print(" to ");
    Serial.print(erase_byte_end);
    Serial.println(")...");
    if (erase_blocks(erase_start, erase_end) == 0) {
        Serial.println("Erase successful.");
        delay(100); // Delay after erase
        // Verify erase by reading
        memset(buffer, 0, BLOCK_SIZE);
        if (read_single_block(erase_start, buffer) == 0) {
            int is_erased = 1;
            uint8_t erase_value = buffer[0]; // Check if all same (0x00 or 0xFF)
            for (int i = 0; i < BLOCK_SIZE; i++) {
                if (buffer[i] != erase_value) {
                    is_erased = 0;
                    break;
                }
            }
            Serial.print("Erase verify (all same value): ");
            Serial.print(is_erased ? "OK" : "Failed");
            Serial.print(" (value 0x");
            Serial.print(erase_value, HEX);
            Serial.println(")");
        } else {
            Serial.println("Erase verify read failed.");
        }
    } else {
        Serial.println("Erase failed.");
    }
    // Performance test: time single block write
    unsigned long start, end;
    start = micros();
    if (write_single_block(arbitrary_addr, buffer) == 0) {
        end = micros();
        double time_taken = (double)(end - start) / 1000000.0;
        Serial.print("Single block write time: ");
        Serial.print(time_taken);
        Serial.println(" seconds");
    } else {
        Serial.println("Performance write failed.");
    }
    // More tests: loop over several arbitrary addresses
    Serial.println("Testing writes/reads at multiple arbitrary addresses...");
    uint32_t test_blocks[] = {500, 1000, 2000};
    int num_tests = sizeof(test_blocks) / sizeof(test_blocks[0]);
    for (int t = 0; t < num_tests; t++) {
        uint32_t block = test_blocks[t];
        uint32_t addr = block * (is_high_capacity ? 1 : BLOCK_SIZE);
        // Write unique pattern
        for (int i = 0; i < BLOCK_SIZE; i++) {
            buffer[i] = (uint8_t)(t * 10 + i % 256);
        }
        Serial.print("Test ");
        Serial.print(t);
        Serial.print(": Write at ");
        Serial.println(addr);
        write_single_block(addr, buffer);
        delay(10);
        // Read verify
        memset(buffer, 0, BLOCK_SIZE);
        read_single_block(addr, buffer);
        int ok = 1;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            if (buffer[i] != (uint8_t)(t * 10 + i % 256)) {
                ok = 0;
                break;
            }
        }
        Serial.print("Test ");
        Serial.print(t);
        Serial.print(" verify: ");
        Serial.println(ok ? "OK" : "Failed");
    }
    Serial.println("All tests completed.");

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to WiFi. IP address: ");
    Serial.println(WiFi.localIP());

    // Set up web server routes
    server.on("/write", HTTP_GET, []() {
        if (server.hasArg("addr") && server.hasArg("value")) {
            String addr_str = server.arg("addr");
            String value_str = server.arg("value");
            uint64_t addr = strtoull(addr_str.c_str(), NULL, 10);
            uint8_t value = (uint8_t)strtoul(value_str.c_str(), NULL, 10);
            int result = write_byte(addr, value);
            if (result == 0) {
                server.send(200, "text/plain", "OK");
            } else {
                server.send(500, "text/plain", "Error writing byte");
            }
        } else {
            server.send(400, "text/plain", "Missing parameters");
        }
    });

    server.on("/read", HTTP_GET, []() {
        if (server.hasArg("addr")) {
            String addr_str = server.arg("addr");
            uint64_t addr = strtoull(addr_str.c_str(), NULL, 10);
            uint8_t value = read_byte(addr);
            server.send(200, "text/plain", String(value));
        } else {
            server.send(400, "text/plain", "Missing parameter");
        }
    });

    server.begin();
    Serial.println("Web server started on port 8023");
}

void loop() {
    server.handleClient();
    delay(1); // Small delay to avoid watchdog issues
}
