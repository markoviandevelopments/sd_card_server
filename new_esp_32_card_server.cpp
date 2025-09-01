#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>

#define CS 5
#define BLOCK_SIZE 512

const char* ssid = "BrubakerWifi"; // Replace with your WiFi SSID
const char* password = "Pre$ton01"; // Replace with your WiFi password

WebServer server(8023);

int is_high_capacity = 0;
static uint8_t block_buffer[BLOCK_SIZE];

SPISettings slowSPI(100000, MSBFIRST, SPI_MODE0); // 100 kHz for init
SPISettings fastSPI(20000000, MSBFIRST, SPI_MODE0); // 20 MHz for data, adjust if needed

// Send non-data command
uint8_t send_command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t* extra_resp, int extra_bytes, bool slow = false) {
    uint8_t response = 0xFF;
    uint8_t resp_bytes[128];
    int r_idx = 0;

    SPI.beginTransaction(slow ? slowSPI : fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(cmd);
    SPI.transfer((arg >> 24) & 0xFF);
    SPI.transfer((arg >> 16) & 0xFF);
    SPI.transfer((arg >> 8) & 0xFF);
    SPI.transfer(arg & 0xFF);
    SPI.transfer(crc);

    for (int i = 0; i < 8; i++) { // Reduced from 64
        resp_bytes[r_idx] = SPI.transfer(0xFF);
        if (resp_bytes[r_idx] != 0xFF) {
            response = resp_bytes[r_idx];
            r_idx++;
            break;
        }
        r_idx++;
    }

    if (extra_resp && extra_bytes > 0) {
        for (int i = 0; i < extra_bytes; i++) {
            extra_resp[i] = SPI.transfer(0xFF);
        }
    }

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    SPI.beginTransaction(fastSPI);
    for (int i = 0; i < 8; i++) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();

    return response;
}

// Read single block
int read_single_block(uint32_t addr, uint8_t* buffer) {
    uint8_t resp = 0xFF;
    uint8_t token = 0xFF;

    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(0x51);
    SPI.transfer((addr >> 24) & 0xFF);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(0xFF);

    for (int i = 0; i < 8; i++) {
        resp = SPI.transfer(0xFF);
        if (resp != 0xFF) break;
    }

    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -1;
    }

    for (int i = 0; i < 1000; i++) { // Reduced from 10000
        token = SPI.transfer(0xFF);
        if (token != 0xFF) break;
    }

    if (token != 0xFE) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -2;
    }

    for (int i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = SPI.transfer(0xFF);
    }

    SPI.transfer(0xFF);
    SPI.transfer(0xFF);

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    return 0;
}

// Write single block
int write_single_block(uint32_t addr, const uint8_t* buffer) {
    uint8_t resp = 0xFF;

    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(0x58);
    SPI.transfer((addr >> 24) & 0xFF);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(0xFF);

    for (int i = 0; i < 8; i++) {
        resp = SPI.transfer(0xFF);
        if (resp != 0xFF) break;
    }

    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -1;
    }

    SPI.transfer(0xFE);

    for (int i = 0; i < BLOCK_SIZE; i++) {
        SPI.transfer(buffer[i]);
    }

    SPI.transfer(0xFF);
    SPI.transfer(0xFF);

    uint8_t data_resp = 0xFF;
    for (int i = 0; i < 8; i++) {
        data_resp = SPI.transfer(0xFF);
        if (data_resp != 0xFF) break;
    }
    data_resp &= 0x1F;

    if (data_resp != 0x05) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -2;
    }

    uint8_t busy = 0x00;
    for (int i = 0; i < 1000000; i++) { // Reduced
        busy = SPI.transfer(0xFF);
        if (busy != 0x00) break;
    }

    if (busy == 0x00) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -3;
    }

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

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

    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(0x52);
    SPI.transfer((addr >> 24) & 0xFF);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(0xFF);

    for (int i = 0; i < 8; i++) {
        resp = SPI.transfer(0xFF);
        if (resp != 0xFF) break;
    }

    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -1;
    }

    for (int block = 0; block < num_blocks; block++) {
        uint8_t token = 0xFF;
        for (int i = 0; i < 1000; i++) {
            token = SPI.transfer(0xFF);
            if (token != 0xFF) break;
        }

        if (token != 0xFE) {
            send_command(0x4C, 0x00000000, 0xFF, NULL, 0); // CMD12
            digitalWrite(CS, HIGH);
            SPI.endTransaction();
            return -2;
        }

        for (int i = 0; i < BLOCK_SIZE; i++) {
            buffer[block * BLOCK_SIZE + i] = SPI.transfer(0xFF);
        }

        SPI.transfer(0xFF);
        SPI.transfer(0xFF);
    }

    resp = send_command(0x4C, 0x00000000, 0xFF, NULL, 0);

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    return 0;
}

// Write multiple blocks (CMD25)
int write_multiple_blocks(uint32_t addr, const uint8_t* buffer, int num_blocks) {
    uint8_t resp = 0xFF;

    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(0x59);
    SPI.transfer((addr >> 24) & 0xFF);
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(0xFF);

    for (int i = 0; i < 8; i++) {
        resp = SPI.transfer(0xFF);
        if (resp != 0xFF) break;
    }

    if (resp != 0x00) {
        digitalWrite(CS, HIGH);
        SPI.endTransaction();
        return -1;
    }

    for (int block = 0; block < num_blocks; block++) {
        SPI.transfer(0xFC);

        for (int i = 0; i < BLOCK_SIZE; i++) {
            SPI.transfer(buffer[block * BLOCK_SIZE + i]);
        }

        SPI.transfer(0xFF);
        SPI.transfer(0xFF);

        uint8_t data_resp = 0xFF;
        for (int i = 0; i < 8; i++) {
            data_resp = SPI.transfer(0xFF);
            if (data_resp != 0xFF) break;
        }
        data_resp &= 0x1F;

        if (data_resp != 0x05) {
            digitalWrite(CS, HIGH);
            SPI.endTransaction();
            return -2;
        }

        uint8_t busy = 0x00;
        for (int i = 0; i < 1000000; i++) {
            busy = SPI.transfer(0xFF);
            if (busy != 0x00) break;
        }

        if (busy == 0x00) {
            digitalWrite(CS, HIGH);
            SPI.endTransaction();
            return -3;
        }
    }

    SPI.transfer(0xFD);

    uint8_t busy = 0x00;
    for (int i = 0; i < 1000000; i++) {
        busy = SPI.transfer(0xFF);
        if (busy != 0x00) break;
    }

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    return 0;
}

// Function to erase blocks (CMD32 + CMD33 + CMD38)
int erase_blocks(uint32_t start_addr, uint32_t end_addr) {
    uint8_t resp;

    resp = send_command(0x60, start_addr, 0xFF, NULL, 0);
    if (resp != 0x00) {
        return -1;
    }

    resp = send_command(0x61, end_addr, 0xFF, NULL, 0);
    if (resp != 0x00) {
        return -2;
    }

    resp = send_command(0x66, 0x00000000, 0xFF, NULL, 0);
    if (resp != 0x00) {
        return -3;
    }

    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    uint8_t busy = 0x00;
    for (int i = 0; i < 1000000; i++) { // Reduced for erase
        busy = SPI.transfer(0xFF);
        if (busy != 0x00) break;
    }

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    if (busy == 0x00) {
        return -4;
    }

    return 0;
}

// get_card_capacity same

void setup() {
    Serial.begin(115200);
    SPI.begin(SCK, MISO, MOSI);

    // Power-on clocks slow
    SPI.beginTransaction(slowSPI);
    digitalWrite(CS, HIGH);
    for (int i = 0; i < 200; i++) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();

    // CMD0 slow
    uint8_t resp;
    int cmd0_retries = 20;
    while (cmd0_retries--) {
        resp = send_command(0x40, 0x00000000, 0x95, NULL, 0, true);
        Serial.print("CMD0 resp: 0x"); Serial.println(resp, HEX);
        if (resp == 0x01) break;
        delay(100);
    }
    if (resp != 0x01) {
        Serial.println("CMD0 failed.");
        while (true);
    }

    // CMD8 slow
    uint8_t cmd8_extra[4];
    resp = send_command(0x48, 0x000001AA, 0x87, cmd8_extra, 4, true);
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

    // Init loop slow
    int retries = 1000;
    while (retries--) {
        if (acmd41_arg == 0x00000000) {
            resp = send_command(0x41, 0x00000000, 0xFF, NULL, 0, true);
        } else {
            resp = send_command(0x77, 0x00000000, 0x65, NULL, 0, true);
            if (resp > 0x01) continue;
            resp = send_command(0x69, acmd41_arg, 0x77, NULL, 0, true);
        }
        if (resp == 0x00) break;
        delay(1);
    }
    if (resp != 0x00) {
        Serial.println("Init timeout.");
        while (true);
    }
    Serial.println("SD initialized.");

    // CMD58 fast
    uint8_t ocr[4];
    resp = send_command(0x7A, 0x00000000, 0xFF, ocr, 4);
    if (resp == 0x00) {
        if (ocr[0] & 0x40) Serial.println("High capacity card.");
    }

    // Fetch CID fast
    SPI.beginTransaction(fastSPI);
    digitalWrite(CS, LOW);

    SPI.transfer(0x4A);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0xFF);

    resp = 0xFF;
    for (int i = 0; i < 8; i++) {
        resp = SPI.transfer(0xFF);
        if (resp != 0xFF) break;
    }

    if (resp == 0x00) {
        uint8_t token = 0xFF;
        for (int i = 0; i < 1000; i++) {
            token = SPI.transfer(0xFF);
            if (token != 0xFF) break;
        }

        if (token == 0xFE) {
            uint8_t cid[16];
            for (int i = 0; i < 16; i++) {
                cid[i] = SPI.transfer(0xFF);
            }
            SPI.transfer(0xFF);
            SPI.transfer(0xFF);
            // Print CID if needed
        }
    }

    digitalWrite(CS, HIGH);
    SPI.endTransaction();

    // Similar for CSD

    // Tests - run with reduced loops, no prints inside functions

    // WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Server routes
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
                server.send(500, "text/plain", "Error");
            }
        } else {
            server.send(400, "text/plain", "Missing");
        }
    });

    server.on("/read", HTTP_GET, []() {
        if (server.hasArg("addr")) {
            String addr_str = server.arg("addr");
            uint64_t addr = strtoull(addr_str.c_str(), NULL, 10);
            uint8_t value = read_byte(addr);
            server.send(200, "text/plain", String(value));
        } else {
            server.send(400, "text/plain", "Missing");
        }
    });

    server.begin();
}

void loop() {
    server.handleClient();
    yield();
}