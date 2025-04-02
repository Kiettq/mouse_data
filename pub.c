#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#define MQTT_HOST "bacaebb643b74603a5f70457b7e684c1.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_TOPIC "KIET/SENSOR/mouse_data"
#define MQTT_USER "KIETMQTT"
#define MQTT_PASS "IoT@12345"
#define CA_FILE "/etc/ssl/certs/ca-certificates.crt"
#define DEVICE_PATH "/dev/my_mouse"

#define HISTORY_SIZE 20
#define ANGLE_THRESHOLD 10.0  // độ

typedef struct {
    int x, y;
    double time_ms;
} MousePoint;

MousePoint history[HISTORY_SIZE];
int history_len = 0;

double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

double distance(int dx, int dy) {
    return sqrt(dx * dx + dy * dy);
}

double angle_between_vectors(int dx1, int dy1, int dx2, int dy2) {
    double dot = dx1 * dx2 + dy1 * dy2;
    double mag1 = sqrt(dx1 * dx1 + dy1 * dy1);
    double mag2 = sqrt(dx2 * dx2 + dy2 * dy2);
    if (mag1 == 0 || mag2 == 0) return 0.0;
    double cos_theta = dot / (mag1 * mag2);
    if (cos_theta > 1.0) cos_theta = 1.0;
    if (cos_theta < -1.0) cos_theta = -1.0;
    return acos(cos_theta) * 180.0 / M_PI;
}

double calc_angle_change() {
    if (history_len < 3) return 0.0;

    double sum_angle = 0.0;
    int angle_count = 0;

    for (int i = 0; i < history_len - 2; i++) {
        int dx1 = history[i + 1].x - history[i].x;
        int dy1 = history[i + 1].y - history[i].y;
        int dx2 = history[i + 2].x - history[i + 1].x;
        int dy2 = history[i + 2].y - history[i + 1].y;

        double angle = angle_between_vectors(dx1, dy1, dx2, dy2);
        sum_angle += angle;
        angle_count++;
    }

    return angle_count > 0 ? sum_angle / angle_count : 0.0;
}

double calc_accuracy() {
    if (history_len < 3) return 1.0;

    int unchanged = 0;
    for (int i = 0; i < history_len - 2; i++) {
        int dx1 = history[i + 1].x - history[i].x;
        int dy1 = history[i + 1].y - history[i].y;
        int dx2 = history[i + 2].x - history[i + 1].x;
        int dy2 = history[i + 2].y - history[i + 1].y;

        double angle = angle_between_vectors(dx1, dy1, dx2, dy2);
        if (angle < ANGLE_THRESHOLD) {
            unchanged++;
        }
    }

    return (double)unchanged / (history_len - 2);
}

int main() {
    struct mosquitto *mosq;
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    mosquitto_tls_set(mosq, CA_FILE, NULL, NULL, NULL, NULL);
    mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);

    if (mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ MQTT connection failed\n");
        return 1;
    }

    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("❌ Failed to open device");
        return 1;
    }

    int last_x = 0, last_y = 0;
    double last_time = get_time_ms();
    double last_speed = 0.0;
    char buffer[256];

    printf("📤 Bắt đầu gửi dữ liệu mouse nâng cao theo PMC8052599...\n");

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            usleep(10000);
            continue;
        }

        int x = 0, y = 0;
        sscanf(buffer, "{\"x\": %d, \"y\": %d}", &x, &y);
        double now = get_time_ms();
        double dt = (now - last_time) / 1000.0;

        double spd = dt > 0 ? distance(x - last_x, y - last_y) / dt : 0;
        double acc = dt > 0 ? (spd - last_speed) / dt : 0;
        double jitter = fabs(spd - last_speed);
        int sudden_stops = (spd < 1.0 && last_speed > 10.0) ? 1 : 0;
        double inactivity_time = (spd < 0.5) ? dt : 0;

        // Cập nhật history
        if (history_len < HISTORY_SIZE) {
            history[history_len++] = (MousePoint){x, y, now};
        } else {
            for (int i = 1; i < HISTORY_SIZE; i++) history[i - 1] = history[i];
            history[HISTORY_SIZE - 1] = (MousePoint){x, y, now};
        }

        double angle_change = calc_angle_change();
        double accuracy = calc_accuracy() * 100.0;

        // Gửi MQTT
        char json[512];
        snprintf(json, sizeof(json),
            "{\"x\": %d, \"y\": %d, \"speed\": %.2f, \"acceleration\": %.2f, \"jitter\": %.2f, "
            "\"angle_change\": %.2f, \"sudden_stops\": %d, \"inactivity_time\": %.2f, \"accuracy\": %.2f}",
            x, y, spd, acc, jitter, angle_change, sudden_stops, inactivity_time, accuracy);

        printf("📨 Gửi: %s\n", json);
        mosquitto_publish(mosq, NULL, MQTT_TOPIC, strlen(json), json, 0, false);

        last_x = x;
        last_y = y;
        last_time = now;
        last_speed = spd;
    }

    close(fd);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}

