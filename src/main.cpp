#include <arpa/inet.h>
#include <coreinit/memory.h>
#include <gx2/texture.h>
#include <jansson.h>
#include <malloc.h>
#include <memory/mappedmemory.h>
#include <netinet/in.h>
#include <notifications/notifications.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <wups.h>
#include <gx2/mem.h>
#include "utils/logger.h"
#include <future>

WUPS_PLUGIN_NAME("Gamepad Streamer");
WUPS_PLUGIN_DESCRIPTION("This plugin allows you to stream the GamePad screen to a PC.");
WUPS_PLUGIN_VERSION("v0.0.1 ALPHA");
WUPS_PLUGIN_AUTHOR("Slushi");
WUPS_PLUGIN_LICENSE("GPL");

#define DEFAULT_IP "192.168.10.1"
#define DEFAULT_PORT 4242
#define DEFAULT_FPS 30
#define DEFAULT_QUALITY 854

char pcIP[16] = DEFAULT_IP;
uint16_t pcPort = DEFAULT_PORT;
uint32_t fps = DEFAULT_FPS;
uint32_t quality = DEFAULT_QUALITY;

bool pluginActive = true;
std::thread transmissionThread;
std::promise<bool> threadInitializationPromise;
NotificationModuleHandle notificationHandle = 0;

static bool copyBufferAndSend(GX2ColorBuffer *sourceBuffer, sockaddr_in &pcAddr, int socket_fd) {
    GX2ColorBuffer targetBuffer = {};
    targetBuffer.surface.width = quality;
    targetBuffer.surface.height = (quality * 480) / 854;
    targetBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;

    GX2CalcSurfaceSizeAndAlignment(&targetBuffer.surface);
    GX2InitColorBufferRegs(&targetBuffer);

    targetBuffer.surface.image = MEMAllocFromMappedMemoryForGX2Ex(
        targetBuffer.surface.imageSize, targetBuffer.surface.alignment);

    if (!targetBuffer.surface.image) {
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, targetBuffer.surface.image, targetBuffer.surface.imageSize);
    GX2CopySurface(&sourceBuffer->surface, 0, 0, &targetBuffer.surface, 0, 0);

    sendto(socket_fd, targetBuffer.surface.image, targetBuffer.surface.imageSize, 0,
           (struct sockaddr *)&pcAddr, sizeof(pcAddr));

    MEMFreeToMappedMemory(targetBuffer.surface.image);
    return true;
}

GX2ColorBuffer *getGamePadBuffer() {
    static GX2ColorBuffer drcBuffer = {};
    drcBuffer.surface.width = 854;
    drcBuffer.surface.height = 480;
    drcBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    return &drcBuffer;
}

void transmissionFunction() {
    try {
        sockaddr_in pcAddr;
        int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (socket_fd < 0) {
            threadInitializationPromise.set_value(false);
            return;
        }

        memset(&pcAddr, 0, sizeof(pcAddr));
        pcAddr.sin_family = AF_INET;
        pcAddr.sin_port = htons(pcPort);
        inet_aton(pcIP, &pcAddr.sin_addr);

        threadInitializationPromise.set_value(true);

        while (pluginActive) {
            GX2ColorBuffer *gamePadBuffer = getGamePadBuffer();
            if (gamePadBuffer) {
                copyBufferAndSend(gamePadBuffer, pcAddr, socket_fd);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
        }

        close(socket_fd);
    } catch (const std::exception &e) {
        threadInitializationPromise.set_value(false);
    }
}

void loadConfigFromJson() {
    char configFilePath[256] = "fs:/vol/external01/wiiu/gamepadStreamerConfig.json";
    FILE *file = fopen(configFilePath, "r");
    if (!file) {
        return;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *jsonData = (char *)malloc(fileSize + 1);
    if (!jsonData) {
        fclose(file);
        return;
    }

    fread(jsonData, 1, fileSize, file);
    jsonData[fileSize] = '\0';
    fclose(file);

    json_error_t error;
    json_t *root = json_loads(jsonData, 0, &error);
    free(jsonData);

    if (!root) {
        return;
    }

    json_t *ipJson = json_object_get(root, "pc_ip");
    json_t *fpsJson = json_object_get(root, "fps");
    json_t *qualityJson = json_object_get(root, "image_quality");

    if (json_is_string(ipJson)) {
        strncpy(pcIP, json_string_value(ipJson), sizeof(pcIP) - 1);
        pcIP[sizeof(pcIP) - 1] = '\0';
    }

    if (json_is_integer(fpsJson)) {
        fps = (uint32_t)json_integer_value(fpsJson);
    }

    if (json_is_integer(qualityJson)) {
        quality = (uint32_t)json_integer_value(qualityJson);
    }

    json_decref(root);
}

INITIALIZE_PLUGIN() {
    loadConfigFromJson();
    pluginActive = true;
    std::future<bool> threadStarted = threadInitializationPromise.get_future();
    transmissionThread = std::thread(transmissionFunction);
    if (!threadStarted.get()) {
        pluginActive = false;
        if (transmissionThread.joinable()) {
            transmissionThread.join();
        }
        return;
    }
}

DEINITIALIZE_PLUGIN() {
    pluginActive = false;
    if (transmissionThread.joinable()) {
        transmissionThread.join();
    }
}

ON_APPLICATION_REQUESTS_EXIT() {
    pluginActive = false;
    if (transmissionThread.joinable()) {
        transmissionThread.join();
    }
}
