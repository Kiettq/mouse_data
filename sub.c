#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <mysql/mysql.h>
#include <json-c/json.h>
#include <unistd.h>

#define MQTT_HOST "bacaebb643b74603a5f70457b7e684c1.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_TOPIC "KIET/SENSOR/mouse_data"
#define MQTT_USER "KIETMQTT"
#define MQTT_PASS "IoT@12345"
#define CA_FILE "/etc/ssl/certs/ca-certificates.crt"

#define DB_HOST "localhost"
#define DB_USER "KIETCDT24"
#define DB_PASS "kiet"
#define DB_NAME "mouse_data"

MYSQL *conn;
struct mosquitto *mosq;

void connect_db() {
    conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "❌ MySQL initialization failed!\n");
        exit(1);
    }
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "❌ MySQL connection error: %s\n", mysql_error(conn));
        exit(1);
    }
    printf("✅ Đã kết nối MySQL!\n");
}

void save_to_db(const char *payload) {
    struct json_object *parsed_json;
    struct json_object *x, *y, *speed, *acceleration, *jitter;
    struct json_object *angle_change, *sudden_stops, *inactivity_time, *accuracy;

    parsed_json = json_tokener_parse(payload);
    if (!parsed_json) {
        fprintf(stderr, "❌ Lỗi phân tích JSON!\n");
        return;
    }

    if (!json_object_object_get_ex(parsed_json, "x", &x) ||
        !json_object_object_get_ex(parsed_json, "y", &y) ||
        !json_object_object_get_ex(parsed_json, "speed", &speed) ||
        !json_object_object_get_ex(parsed_json, "acceleration", &acceleration) ||
        !json_object_object_get_ex(parsed_json, "jitter", &jitter) ||
        !json_object_object_get_ex(parsed_json, "angle_change", &angle_change) ||
        !json_object_object_get_ex(parsed_json, "sudden_stops", &sudden_stops) ||
        !json_object_object_get_ex(parsed_json, "inactivity_time", &inactivity_time) ||
        !json_object_object_get_ex(parsed_json, "accuracy", &accuracy)) {
        fprintf(stderr, "❌ Thiếu trường dữ liệu trong JSON!\n");
        json_object_put(parsed_json);
        return;
    }

    char query[512];
    snprintf(query, sizeof(query),
        "INSERT INTO mouse_data (x, y, speed, acceleration, jitter, angle_change, sudden_stops, inactivity_time, accuracy, timestamp) "
        "VALUES (%d, %d, %.2f, %.2f, %.2f, %.2f, %d, %.2f, %.2f, NOW())",
        json_object_get_int(x), json_object_get_int(y),
        json_object_get_double(speed), json_object_get_double(acceleration),
        json_object_get_double(jitter), json_object_get_double(angle_change),
        json_object_get_int(sudden_stops), json_object_get_double(inactivity_time),
        json_object_get_double(accuracy));

    if (mysql_query(conn, query)) {
        fprintf(stderr, "❌ Lỗi MySQL: %s\n", mysql_error(conn));
    } else 

    json_object_put(parsed_json);
}

void message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
    printf("📩 Nhận dữ liệu từ MQTT: %s\n", (char *)msg->payload);

    // Kiểm tra nếu là thông điệp mất kết nối
    if (strstr(msg->payload, "device_disconnected")) {
        printf("⚠️  Thiết bị chuột đã ngắt kết nối!\n");
        return; // Không lưu vào database
    }

    // Nếu là dữ liệu hợp lệ, lưu vào database
    save_to_db((char *)msg->payload);
}

int main() {
    connect_db();
    mosquitto_lib_init();

    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "❌ Lỗi tạo mosquitto instance!\n");
        mysql_close(conn);
        return 1;
    }

    mosquitto_tls_set(mosq, CA_FILE, NULL, NULL, NULL, NULL);
    mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);
    mosquitto_message_callback_set(mosq, message_callback);

    if (mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ MQTT connection failed!\n");
        mosquitto_destroy(mosq);
        mysql_close(conn);
        return 1;
    }

    mosquitto_subscribe(mosq, NULL, MQTT_TOPIC, 0);

    printf("🔍 Đang lắng nghe dữ liệu từ: %s...\n", MQTT_TOPIC);
    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    mysql_close(conn);

    return 0;
}

