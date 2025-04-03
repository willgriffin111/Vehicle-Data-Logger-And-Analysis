#include "server.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <iostream>

extern SdFat SD;
WebServer server(80);

bool deleteRecursively(const char* path);

void createDummyFileIfNotExists() {
    if (!SD.exists("/test/dummy.json")) {
      Serial.println("Dummy file not found, creating it...");
      FsFile file = SD.open("/test/dummy.json", O_WRITE | O_CREAT);
      if (file) {
        file.print(
          "{\"gps\":{\"time\":\"16:09:32\",\"latitude\":40.7590,\"longitude\":-73.9860},\"obd\":{\"rpm\":0,\"speed\":0,\"maf\":0.94,\"instant_mpg\":0,\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":-19,\"accel_y\":-4}}\n"
          "{\"gps\":{\"time\":\"16:09:35\",\"latitude\":40.7590,\"longitude\":-73.9850},\"obd\":{\"rpm\":217,\"speed\":0,\"maf\":2.97,\"instant_mpg\":0,\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":3,\"accel_y\":0}}\n"
          "{\"gps\":{\"time\":\"16:09:38\",\"latitude\":40.7580,\"longitude\":-73.9850},\"obd\":{\"rpm\":772,\"speed\":0,\"maf\":8.33,\"instant_mpg\":0,\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":1,\"accel_y\":3}}\n"
          "{\"gps\":{\"time\":\"16:09:41\",\"latitude\":40.7580,\"longitude\":-73.9860},\"obd\":{\"rpm\":778,\"speed\":0,\"maf\":8.16,\"instant_mpg\":0,\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":-1,\"accel_y\":0}}"
        );
        file.close();
        Serial.println("Dummy file created.");
      } else {
        Serial.println("Failed to create dummy file.");
      }
    } else {
      Serial.println("Dummy file already exists.");
    }
  }

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Connected");
}

void handleDays() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  Serial.println("Listing available days...");

  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    FsFile root = SD.open("/");
    if (!root) {
      Serial.println("Failed to open root directory");
      server.send(500, "text/plain", "Failed to open root directory");
      xSemaphoreGive(sdMutex);
      return;
    }

    String json = "[";
    bool first = true;

    while (true) {
      FsFile entry = root.openNextFile();
      if (!entry)
        break;

      if (entry.isDir()) {
        char nameBuffer[32];
        entry.getName(nameBuffer, sizeof(nameBuffer));

        if (nameBuffer[0] == '.') {
          entry.close();
          continue;
        }

        if (!first)
          json += ",";
        json += "\"" + String(nameBuffer) + "\"";
        first = false;
      }
      entry.close();
    }
    json += "]";
    root.close();

    server.send(200, "application/json", json);
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("SD Mutex timeout in handleDays()");
    server.send(500, "text/plain", "SD card access timeout");
  }
}

void handleDrives() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("day")) {
    server.send(400, "text/plain", "Missing 'day' parameter");
    return;
  }

  String day = server.arg("day");
  Serial.print("Listing drives for day: ");
  Serial.println(day);

  String path = "/" + day;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    FsFile dayDir = SD.open(path.c_str());
    if (!dayDir || !dayDir.isDir()) {
      Serial.println("Day folder not found");
      server.send(404, "text/plain", "Day folder not found");
      xSemaphoreGive(sdMutex);
      return;
    }

    String json = "[";
    bool first = true;
    while (true) {
      FsFile entry = dayDir.openNextFile();
      if (!entry)
        break;

      if (!entry.isDir()) {
        char nameBuffer[32];
        entry.getName(nameBuffer, sizeof(nameBuffer));

        if (nameBuffer[0] == '.') {
          entry.close();
          continue;
        }

        if (!first)
          json += ",";
        json += "\"" + String(nameBuffer) + "\"";
        first = false;
      }
      entry.close();
    }
    json += "]";
    dayDir.close();

    server.send(200, "application/json", json);
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("SD Mutex timeout in handleDrives()");
    server.send(500, "text/plain", "SD card access timeout");
  }
}

void handleDrive() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("day") || !server.hasArg("drive")) {
    server.send(400, "text/plain", "Missing 'day' or 'drive' parameter");
    return;
  }
  String day = server.arg("day");
  String driveFile = server.arg("drive");

  if (day.startsWith(".") || driveFile.startsWith(".")) {
    server.send(403, "text/plain", "Access forbidden");
    return;
  }

  String path = "/" + day + "/" + driveFile;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    FsFile file = SD.open(path.c_str(), O_READ);
    if (!file) {
      server.send(404, "text/plain", "Drive file not found");
      xSemaphoreGive(sdMutex);
      return;
    }

    // Manually stream the file in chunks:
    server.sendHeader("Content-Type", "application/json");
    server.setContentLength(file.size());
    server.send(200);

    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    while (file.available()) {
      size_t bytesRead = file.read(buffer, bufferSize);
      server.sendContent((const char*)buffer, bytesRead);
    }
    file.close();
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("SD Mutex timeout in handleDrive()");
    server.send(500, "text/plain", "SD card access timeout");
  }
}

void handleLiveData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  Serial.println("Fetching latest drive data...");

  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000))) {
    FsFile root = SD.open("/");
    if (!root) {
      server.send(500, "text/plain", "Failed to open root directory");
      xSemaphoreGive(sdMutex);
      return;
    }
    String latestDay = "";
    while (true) {
      FsFile entry = root.openNextFile();
      if (!entry)
        break;
      if (entry.isDir()) {
        char nameBuffer[32];
        entry.getName(nameBuffer, sizeof(nameBuffer));
        if (strlen(nameBuffer) == 10 && nameBuffer[4] == '-' && nameBuffer[7] == '-') {
          if (latestDay == "" || String(nameBuffer) > latestDay) {
            latestDay = nameBuffer;
          }
        }
      }
      entry.close();
    }
    root.close();
    if (latestDay == "") {
      server.send(404, "text/plain", "No log data found");
      xSemaphoreGive(sdMutex);
      return;
    }
    String path = "/" + latestDay;
    FsFile dayDir = SD.open(path.c_str());
    if (!dayDir || !dayDir.isDir()) {
      server.send(500, "text/plain", "Could not access latest day folder");
      xSemaphoreGive(sdMutex);
      return;
    }
    String latestDrive = "";
    while (true) {
      FsFile entry = dayDir.openNextFile();
      if (!entry)
        break;
      if (!entry.isDir()) {
        char nameBuffer[32];
        entry.getName(nameBuffer, sizeof(nameBuffer));
        if (strlen(nameBuffer) >= 12 && nameBuffer[2] == '-' && nameBuffer[5] == '-' && strstr(nameBuffer, ".json")) {
          if (latestDrive == "" || String(nameBuffer) > latestDrive) {
            latestDrive = nameBuffer;
          }
        }
      }
      entry.close();
    }
    dayDir.close();

    if (latestDrive == "") {
      server.send(404, "text/plain", "No latest drive data found");
      xSemaphoreGive(sdMutex);
      return;
    }

    String fullPath = "/" + latestDay + "/" + latestDrive;
    FsFile file = SD.open(fullPath.c_str(), O_READ);
    if (!file) {
      server.send(404, "text/plain", "Latest drive file not found");
      xSemaphoreGive(sdMutex);
      return;
    }

    server.sendHeader("Content-Type", "application/json");
    server.setContentLength(file.size());
    server.send(200);

    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    while (file.available()) {
      size_t bytesRead = file.read(buffer, bufferSize);
      server.sendContent((const char*)buffer, bytesRead);
    }
    file.close();
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("SD Mutex timeout in handleLiveData()");
    server.send(500, "text/plain", "SD busy, try again later");
  }
}

void handleSDInfo() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  Serial.println("Fetching SD diagnostics...");

  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    String json = "{";

    // Retrieve volume information from the SD card.
    FsVolume* vol = SD.vol();
    if (!vol) {
      json += "\"sd_status\": \"Not detected\"";
    } else {
      uint32_t sectorsPerCluster = vol->sectorsPerCluster();
      uint32_t clusterCount      = vol->clusterCount();
      uint32_t freeClusters      = vol->freeClusterCount();

      uint64_t totalSize = (uint64_t)clusterCount * sectorsPerCluster * 512;
      uint64_t freeSize  = (uint64_t)freeClusters * sectorsPerCluster * 512;
      uint64_t usedSize  = totalSize - freeSize;

      json += "\"sd_status\": \"OK\",";
      json += "\"total_size\": " + String(totalSize / (1024.0 * 1024.0), 2) + ",";
      json += "\"used_size\": "  + String(usedSize / (1024.0 * 1024.0), 2) + ",";
      json += "\"free_size\": "  + String(freeSize / (1024.0 * 1024.0), 2) + ",";
    }

    json += "\"esp32_uptime_sec\": " + String(millis() / 1000) + "}";
    server.send(200, "application/json", json);
    xSemaphoreGive(sdMutex);
  } else {
    Serial.println("SD Mutex timeout in handleSDInfo()");
    server.send(500, "text/plain", "SD card access timeout");
  }
}

void handleDelete() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing 'path' parameter");
    return;
  }

  String path = server.arg("path");

  if (!path.startsWith("/")) {
    server.send(400, "text/plain", "Invalid path: must start with '/'");
    return;
  }

  if (path.indexOf("..") != -1) {
    server.send(403, "text/plain", "Access forbidden");
    return;
  }

  if (path.length() > 1 && path.charAt(1) == '.') {
    server.send(403, "text/plain", "Access forbidden");
    return;
  }

  Serial.printf("Deleting path: %s\n", path.c_str());

  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    bool success = deleteRecursively(path.c_str());
    xSemaphoreGive(sdMutex);
    if (success) {
      server.send(200, "text/plain", "Deleted successfully");
    } else {
      server.send(500, "text/plain", "Failed to delete (path may not exist or an error occurred)");
    }
  } else {
    Serial.println("SD Mutex timeout in handleDelete()");
    server.send(500, "text/plain", "SD card access timeout");
  }
}

bool deleteRecursively(const char* path) {
  FsFile file = SD.open(path);
  if (!file) {
    Serial.printf("Path not found: %s\n", path);
    return false;
  }

  if (file.isDir()) {
    while (true) {
      FsFile entry = file.openNextFile();
      if (!entry)
        break;

      char entryName[32];
      entry.getName(entryName, sizeof(entryName));
      if (strcmp(entryName, ".") == 0 || strcmp(entryName, "..") == 0) {
        entry.close();
        continue;
      }

      String fullPath = String(path) + "/" + String(entryName);
      if (entry.isDir()) {
        if (!deleteRecursively(fullPath.c_str())) {
          entry.close();
          file.close();
          return false;
        }
      } else {
        if (!SD.remove(fullPath.c_str())) {
          Serial.printf("Failed to delete file: %s\n", fullPath.c_str());
          entry.close();
          file.close();
          return false;
        }
      }
      entry.close();
    }
    file.close();
    if (!SD.rmdir(path)) {
      Serial.printf("Failed to remove directory: %s\n", path);
      return false;
    }
    return true;
  } else {
    file.close();
    if (!SD.remove(path)) {
      Serial.printf("Failed to remove file: %s\n", path);
      return false;
    }
    return true;
  }
}

void handleDeleteOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200);
}

void setupServer() {
  Serial.println("Setting up Web Server...");

  createDummyFileIfNotExists();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/days", HTTP_GET, handleDays);
  server.on("/drives", HTTP_GET, handleDrives);
  server.on("/drive", HTTP_GET, handleDrive);
  server.on("/live", HTTP_GET, handleLiveData);
  server.on("/sdinfo", HTTP_GET, handleSDInfo);
  server.on("/delete", HTTP_OPTIONS, handleDeleteOptions);
  server.on("/delete", HTTP_DELETE, handleDelete);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Increase signal strength

  server.begin();
  Serial.println("Web server started.");
}
