#include <arpa/inet.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <gx2/texture.h>
#include <jansson.h>
#include <malloc.h>
#include <memory/mappedmemory.h>
#include <netinet/in.h>
#include <notifications/notifications.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <wups.h>

#include "utils/logger.h"
#include <whb/proc.h>

WUPS_PLUGIN_NAME("Gamepad Streamer");
WUPS_PLUGIN_DESCRIPTION("This plugin allows you to stream the GamePad screen to a PC.");
WUPS_PLUGIN_VERSION("v0.0.1 ALPHA");
WUPS_PLUGIN_AUTHOR("Slushi");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();

#define DEFAULT_IP      "192.168.10.1"
#define DEFAULT_PORT    4242
#define DEFAULT_FPS     30
#define DEFAULT_QUALITY 854

char pcIP[16]    = DEFAULT_IP;
uint16_t pcPort  = DEFAULT_PORT;
uint32_t fps     = DEFAULT_FPS;
uint32_t quality = DEFAULT_QUALITY;

GX2Texture *drcTex = nullptr;
bool pluginActive  = true;
OSThread threadHandle;

NotificationModuleHandle notificationHandle = 0;

int initializeSocket(const char *ip, uint16_t port, sockaddr_in &addr) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        std::string errorMessage = "Could not create socket.";
        NotificationModule_UpdateDynamicNotificationText(notificationHandle, errorMessage.c_str());
        NotificationModule_UpdateDynamicNotificationBackgroundColor(notificationHandle, {237, 28, 36, 255});
        NotificationModule_FinishDynamicNotificationWithShake(notificationHandle, 2.0f, 0.5f);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_aton(ip, &addr.sin_addr);

    return socket_fd;
}

void sendGamePadImageToPC(GX2Texture *drcTex, int socket_fd, sockaddr_in &addr) {
    if (!drcTex || !drcTex->surface.image) {
        return;
    }

    void *data = MEMAllocFromMappedMemory(drcTex->surface.imageSize);
    if (!data) {
        std::string errorMessage = "Can't allocate memory for GamePad image.";
        NotificationModule_UpdateDynamicNotificationText(notificationHandle, errorMessage.c_str());
        NotificationModule_UpdateDynamicNotificationBackgroundColor(notificationHandle, {237, 28, 36, 255});
        NotificationModule_FinishDynamicNotificationWithShake(notificationHandle, 2.0f, 0.5f);
    }

    memcpy(data, drcTex->surface.image, drcTex->surface.imageSize);
    sendto(socket_fd, data, drcTex->surface.imageSize, 0, (struct sockaddr *) &addr, sizeof(addr));

    MEMFreeToMappedMemory(data);
}

int transmissionThread(int argc, const char **argv) {
    sockaddr_in pcAddr;
    int socket_fd = initializeSocket(pcIP, pcPort, pcAddr);

    while (pluginActive) {
        if (drcTex && drcTex->surface.image) {
            sendGamePadImageToPC(drcTex, socket_fd, pcAddr);
        }
        OSSleepTicks(OSMillisecondsToTicks(1000 / fps));
    }

    close(socket_fd);
    return 0;
}

void setupGamePadTexture() {
    if (!drcTex) {
        drcTex = (GX2Texture *) MEMAllocFromMappedMemoryForGX2Ex(sizeof(GX2Texture), 0x40);
        if (!drcTex) {
            std::string errorMessage = "Error while allocating GamePad texture.";
            NotificationModule_UpdateDynamicNotificationText(notificationHandle, errorMessage.c_str());
            NotificationModule_UpdateDynamicNotificationBackgroundColor(notificationHandle, {237, 28, 36, 255});
            NotificationModule_FinishDynamicNotificationWithShake(notificationHandle, 2.0f, 0.5f);
        }

        drcTex->surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        drcTex->surface.width     = quality;
        drcTex->surface.height    = quality * 480 / 854;
        drcTex->surface.depth     = 1;
        drcTex->surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
        drcTex->surface.imageSize = drcTex->surface.width * drcTex->surface.height * 4;

        GX2InitTextureRegs(drcTex);
        drcTex->surface.use = (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        if (drcTex->surface.imageSize) {
            drcTex->surface.image = MEMAllocFromMappedMemoryForGX2Ex(
                    drcTex->surface.imageSize,
                    drcTex->surface.alignment);

            if (!drcTex->surface.image) {
                std::string errorMessage = "Error while allocating GamePad image";
                NotificationModule_UpdateDynamicNotificationText(notificationHandle, errorMessage.c_str());
                NotificationModule_UpdateDynamicNotificationBackgroundColor(notificationHandle, {237, 28, 36, 255});
                NotificationModule_FinishDynamicNotificationWithShake(notificationHandle, 2.0f, 0.5f);
            }
        }
    }
}

void loadConfigFromJson() {
    char configFilePath[256] = "fs:/vol/external01/wiiu/environments/aroma/plugins/gamepadStreamerConfig.json";

    FILE *file = fopen(configFilePath, "r");
    if (!file) {
        OSFatal("[gamepadStreamer - loadConfigFromJson] Failed to open configuration file");
        return;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *jsonData = (char *) malloc(fileSize + 1);
    fread(jsonData, 1, fileSize, file);
    jsonData[fileSize] = '\0';
    fclose(file);

    json_error_t error;
    json_t *root = json_loads(jsonData, 0, &error);
    if (!root) {
        OSFatal("[gamepadStreamer - loadConfigFromJson] Error parsing JSON: ");
        free(jsonData);
        return;
    }

    json_t *ipJson      = json_object_get(root, "pc_ip");
    json_t *fpsJson     = json_object_get(root, "fps");
    json_t *qualityJson = json_object_get(root, "image_quality");

    if (json_is_string(ipJson)) {
        strncpy(pcIP, json_string_value(ipJson), sizeof(pcIP) - 1);
        pcIP[sizeof(pcIP) - 1] = '\0';
    }
    if (json_is_integer(fpsJson)) {
        fps = (uint32_t) json_integer_value(fpsJson);
    }
    if (json_is_integer(qualityJson)) {
        quality = (uint32_t) json_integer_value(qualityJson);
    }

    json_decref(root);
    free(jsonData);
}

INITIALIZE_PLUGIN() {
    initLogging();
    setupGamePadTexture();
    loadConfigFromJson();
    pluginActive = true;

    char stack[0x2000];
    if (OSCreateThread(&threadHandle, transmissionThread, 0, nullptr, stack, sizeof(stack), 16, OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        OSResumeThread(&threadHandle);
    } else {
        OSFatal("[gamepadStreamer - INITIALIZE_PLUGIN] Failed to create transmission thread");
    }
    deinitLogging();

    std::string errorMessage = "GamePadStreamer initialized";
    NotificationModule_UpdateDynamicNotificationText(notificationHandle, errorMessage.c_str());
    NotificationModule_UpdateDynamicNotificationBackgroundColor(notificationHandle, {143, 217, 209, 255});
    NotificationModule_FinishDynamicNotificationWithShake(notificationHandle, 2.0f, 0.5f);
}

DEINITIALIZE_PLUGIN() {
    pluginActive = false;
    OSJoinThread(&threadHandle, nullptr);
    if (drcTex && drcTex->surface.image) {
        MEMFreeToMappedMemory(drcTex->surface.image);
    }
    if (drcTex) {
        MEMFreeToMappedMemory(drcTex);
    }
    DEBUG_FUNCTION_LINE("DEINITIALIZE_PLUGIN of gamepadStreamer!");
}

ON_APPLICATION_REQUESTS_EXIT() {
    pluginActive = false;
    OSJoinThread(&threadHandle, nullptr);
    if (drcTex && drcTex->surface.image) {
        MEMFreeToMappedMemory(drcTex->surface.image);
    }
    if (drcTex) {
        MEMFreeToMappedMemory(drcTex);
    }
    DEBUG_FUNCTION_LINE("ON_APPLICATION_REQUESTS_EXIT of gamepadStreamer!");
}