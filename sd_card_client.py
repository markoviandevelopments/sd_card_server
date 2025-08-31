import requests
import random

# Replace with the IP address printed by the ESP32
esp_ip = "192.168.1.250"  # Example IP, replace with actual

# Choose an arbitrary byte address (e.g., 1024 to avoid boot sector)
byte_addr = 1024

# Generate random value between 0 and 255
random_value = random.randint(0, 255)
print(f"Writing value {random_value} to address {byte_addr}")

# Write request
def write():
    write_url = f"http://{esp_ip}:8023/write?addr={byte_addr}&value={random_value}"
    write_response = requests.get(write_url)
    if write_response.status_code == 200:
        print("Write successful")
    else:
        print(f"Write failed: {write_response.text}")
        exit()

# Read request to verify
read_url = f"http://{esp_ip}:8023/read?addr={byte_addr}"
read_response = requests.get(read_url)
if read_response.status_code == 200:
    read_value = int(read_response.text)
    if read_value == random_value:
        print(f"Verification successful: Read value {read_value}")
    else:
        print(f"Verification failed: Read value {read_value}, expected {random_value}")
else:
    print(f"Read failed: {read_response.text}")
