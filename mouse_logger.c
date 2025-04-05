#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define DEVICE_NAME "my_mouse"
#define CLASS_NAME  "mouse_class"
#define MAX_RECORDS 100
#define RECORD_LEN  128

static struct input_handler my_mouse_handler;
static struct input_handle *global_handle = NULL;

static int major_number;
static struct class*  mouse_class  = NULL;
static struct device* mouse_device = NULL;

static char record_buf[MAX_RECORDS][RECORD_LEN];
static int head = 0;
static int tail = 0;
static int count = 0;

static DEFINE_MUTEX(mouse_mutex);
static DECLARE_WAIT_QUEUE_HEAD(mouse_waitqueue);

static bool device_connected = false;
static bool data_available = false;

//
// Device read: copy 1 bản ghi từ buffer ra userspace
//
static ssize_t dev_read(struct file *filep, char *user_buffer, size_t len, loff_t *offset) {
    char temp[RECORD_LEN];
    ssize_t ret = 0;

    // Nếu thiết bị không kết nối, trả về lỗi ngay
    if (!device_connected) {
        return -ENODEV;
    }

    // Chờ cho đến khi có dữ liệu mới hoặc thiết bị bị rút
    wait_event_interruptible(mouse_waitqueue, count > 0 || !device_connected);

    if (!device_connected) {
        return -ENODEV;
    }

    mutex_lock(&mouse_mutex);

    if (count == 0) {
        mutex_unlock(&mouse_mutex);
        return 0; // Không có dữ liệu
    }

    strncpy(temp, record_buf[tail], RECORD_LEN);
    tail = (tail + 1) % MAX_RECORDS;
    count--;

    mutex_unlock(&mouse_mutex);

    size_t temp_len = strnlen(temp, RECORD_LEN);
    if (len > temp_len) len = temp_len;

    if (copy_to_user(user_buffer, temp, len)) return -EFAULT;
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = dev_read,
};

//
// Ghi bản ghi mới vào buffer khi có EV_SYN
//
static void my_mouse_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    static int rel_x = 0, rel_y = 0;
    char json[RECORD_LEN];

    if (type == EV_REL) {
        if (code == REL_X) rel_x = value;
        else if (code == REL_Y) rel_y = value;
    }

    if (type == EV_SYN) {
        snprintf(json, RECORD_LEN, "{\"x\": %d, \"y\": %d}\n", rel_x, rel_y);

        mutex_lock(&mouse_mutex);

        strncpy(record_buf[head], json, RECORD_LEN);
        head = (head + 1) % MAX_RECORDS;

        if (count < MAX_RECORDS)
            count++;
        else
            tail = (tail + 1) % MAX_RECORDS;  // ghi đè cái cũ nhất

        data_available = true;
        mutex_unlock(&mouse_mutex);

        wake_up_interruptible(&mouse_waitqueue);

        printk(KERN_INFO "📤 Mouse JSON: %s", json);
    }
}

//
// Gắn handler vào chuột
//
static int my_mouse_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;

    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle) return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "my_mouse_handle";

    if (input_register_handle(handle) || input_open_device(handle)) {
        kfree(handle);
        return -EINVAL;
    }

    global_handle = handle;
    device_connected = true;
    data_available = false;

    wake_up_interruptible(&mouse_waitqueue);

    printk(KERN_INFO "🖱️  Mouse connected: %s\n", dev->name);
    return 0;
}

static void my_mouse_disconnect(struct input_handle *handle) {
    printk(KERN_INFO "🛑 Mouse disconnected\n");
    device_connected = false;

    wake_up_interruptible(&mouse_waitqueue);

    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id my_mouse_ids[] = {
    { .driver_info = 1 }, {}, // sentinel
};

static struct input_handler my_mouse_handler = {
    .event     = my_mouse_event,
    .connect   = my_mouse_connect,
    .disconnect = my_mouse_disconnect,
    .name      = "my_mouse_handler",
    .id_table  = my_mouse_ids,
};

//
// Khởi tạo module
//
static int __init my_mouse_init(void) {
    printk(KERN_INFO "🔌 Loading Mouse Logger v4.0 (with Disconnect Detection)...\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) return major_number;

    mouse_class = class_create(CLASS_NAME);
    if (IS_ERR(mouse_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(mouse_class);
    }

    mouse_device = device_create(mouse_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(mouse_device)) {
        class_destroy(mouse_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(mouse_device);
    }

    mutex_init(&mouse_mutex);
    return input_register_handler(&my_mouse_handler);
}

//
// Cleanup khi remove module
//
static void __exit my_mouse_exit(void) {
    input_unregister_handler(&my_mouse_handler);
    device_destroy(mouse_class, MKDEV(major_number, 0));
    class_unregister(mouse_class);
    class_destroy(mouse_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    mutex_destroy(&mouse_mutex);

    printk(KERN_INFO "👋 Unloaded Mouse Logger v4.0\n");
}

module_init(my_mouse_init);
module_exit(my_mouse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KIETGPT");
MODULE_DESCRIPTION("Mouse Logger Kernel Module v4.0 with Circular Buffer and Device Disconnect Detection");

