// Board command sequence derived from Waveshare revision 294543798f1a44e2f2c4d2976522323f2beee11d.
#include "board.hpp"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
namespace board {
static esp_lcd_panel_io_handle_t io;
static esp_lcd_touch_handle_t touch;
static i2c_master_dev_handle_t pmic;
static TouchFilter filter = nullptr;
static esp_lcd_panel_handle_t physical_panel;
static esp_lcd_panel_t rotated_panel{};
static uint16_t *rotation_buffer;
static constexpr size_t StripePixels = 480 * 12;
static std::atomic<spark::Orientation> angle{spark::Orientation::Upright};
static i2c_master_dev_handle_t imu;
static std::atomic<bool> imu_ok{false};
static uint64_t imu_retry_at = 0;
static bool imu_initialized = false;
static esp_err_t imu_read(uint8_t reg, uint8_t *data, size_t n) {
    return i2c_master_transmit_receive(imu, &reg, 1, data, n, 10);
}
static bool imu_configure() {
    uint8_t id = 0;
    if (!imu || imu_read(0, &id, 1) != ESP_OK || id != 0x05)
        return false;
    // Exact-board QMI8658 reference: auto increment, +/-2 g, 62.5 Hz,
    // accelerometer only. No PMIC changes and no gyroscope needed.
    const uint8_t writes[][2] = {{0x08, 0}, {0x02, 0x60}, {0x03, 0x07}, {0x08, 1}};
    for (const auto &w : writes)
        if (i2c_master_transmit(imu, w, 2, 10) != ESP_OK)
            return false;
    return true;
}
bool acceleration(spark::Acceleration &a) {
    uint64_t now = esp_timer_get_time() / 1000;
    if (now < imu_retry_at)
        return false;
    if (!imu_initialized) {
        imu_initialized = imu_configure();
        imu_retry_at = now + (imu_initialized ? 50 : 5000);
        imu_ok = false;
        return false;
    }
    uint8_t b[6], status;
    if (imu_read(0x2e, &status, 1) != ESP_OK || !(status & 1) ||
        imu_read(0x35, b, sizeof b) != ESP_OK) {
        imu_ok = false;
        imu_initialized = false;
        imu_retry_at = now + 1000;
        return false;
    }
    const auto axis = [&](int i) {
        return int16_t(uint16_t(b[i]) | (uint16_t(b[i + 1]) << 8)) / 16384.0f;
    };
    // Sensor-to-display mounting transform. Physical calibration recorded in hardware notes.
    a = {axis(0), axis(2), axis(4)};
    imu_ok = true;
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
    static uint64_t next_log = 0;
    if (now >= next_log) {
        ESP_LOGI("qa_imu", "x_mg=%d y_mg=%d z_mg=%d", int(a.x * 1000), int(a.y * 1000),
                 int(a.z * 1000));
        next_log = now + 1000;
    }
#endif
    return true;
}
bool rotation_available() {
    return imu_ok.load() && rotation_buffer;
}
spark::Orientation orientation() {
    return angle.load();
}
bool set_orientation(spark::Orientation next) {
    if (next == angle.load())
        return true;
    if (!rotation_buffer)
        return false;
    // Called only by the LVGL lock owner, outside rendering and active gestures.
    if (esp_lcd_panel_io_tx_param(io, -1, nullptr, 0) != ESP_OK)
        return false;
    angle = next;
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI("board", "orientation=%u", unsigned(next) * 90);
    return true;
}
static esp_err_t draw_rotated(esp_lcd_panel_t *, int x1, int y1, int x2, int y2,
                              const void *pixels) {
    const auto rotation = angle.load();
    if (rotation == spark::Orientation::Upright)
        return esp_lcd_panel_draw_bitmap(physical_panel, x1, y1, x2, y2, pixels);
    if (!rotation_buffer || x2 <= x1 || y2 <= y1 ||
        size_t(x2 - x1) * size_t(y2 - y1) > StripePixels)
        return ESP_ERR_INVALID_SIZE;
    // LVGL waits for the IO completion callback before reusing the single draw buffer.
    // Explicitly drain before touching the scratch buffer as an additional lifetime guard.
    auto err = esp_lcd_panel_io_tx_param(io, -1, nullptr, 0);
    if (err != ESP_OK)
        return err;
    spark::rotate_pixels(static_cast<const uint16_t *>(pixels), rotation_buffer, x2 - x1, y2 - y1,
                         rotation);
    auto r = spark::rotate_rect({x1, y1, x2, y2}, rotation);
    return esp_lcd_panel_draw_bitmap(physical_panel, r.x1, r.y1, r.x2, r.y2, rotation_buffer);
}
static void write_reg(uint8_t r, uint8_t v) {
    uint8_t b[] = {r, v};
    ESP_ERROR_CHECK(i2c_master_transmit(pmic, b, 2, 1000));
}
static uint8_t read_reg(uint8_t r) {
    uint8_t v;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(pmic, &r, 1, &v, 1, 1000));
    return v;
}
static void aldo3(bool enabled) {
    uint8_t v = read_reg(0x90);
    write_reg(0x90, enabled ? v | 4 : v & ~4);
    vTaskDelay(pdMS_TO_TICKS(100));
}
void brightness(unsigned n) {
    uint8_t value = std::min(n, 100u) * 255 / 100;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x02005100, &value, 1));
}
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
void wait_transfer() {
    // IDF SPI tx_param drains queued transfers; a negative command with no data
    // performs no panel write. Call only from the LVGL task/lock owner.
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, -1, nullptr, 0));
}
#endif
bool lock(int timeout) {
    return esp_lv_adapter_lock(timeout) == ESP_OK;
}
void unlock() {
    esp_lv_adapter_unlock();
}
void set_touch_filter(TouchFilter f) {
    filter = f;
}
static void read_touch(lv_indev_t *, lv_indev_data_t *data) {
    uint16_t x = 0, y = 0, strength = 0;
    uint8_t count = 0;
    bool down = false;
    if (esp_lcd_touch_read_data(touch) == ESP_OK)
        down = esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1) && count;
    if (down && (x >= 480 || y >= 480))
        down = false;
    auto point = spark::unrotate_point({x, y}, angle.load());
    x = point.x;
    y = point.y;
    bool consume = filter && filter(down, x, y);
    data->state = down && !consume ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
}
static void round_area(lv_event_t *e) {
    auto *a = static_cast<lv_area_t *>(lv_event_get_param(e));
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 = std::min<int32_t>(479, a->x2 | 1);
    a->y2 = std::min<int32_t>(479, a->y2 | 1);
}
void init() {
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_8;
    bus.scl_io_num = GPIO_NUM_7;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t i2c;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c));
    i2c_device_config_t pc{};
    pc.device_address = 0x34;
    pc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    pc.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c, &pc, &pmic));
    // Only ALDO3 (panel reset) voltage is modified. All charger and unrelated rail registers are
    // preserved.
    uint8_t v = read_reg(0x94);
    write_reg(0x94, (v & 0xe0) | 28);
    aldo3(true);
    aldo3(false);
    aldo3(true);
    spi_bus_config_t sb{};
    sb.sclk_io_num = 0;
    sb.data0_io_num = 1;
    sb.data1_io_num = 2;
    sb.data2_io_num = 3;
    sb.data3_io_num = 4;
    sb.max_transfer_sz = 480 * 24 * 2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &sb, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t ic{};
    ic.cs_gpio_num = 15;
    ic.dc_gpio_num = -1;
    ic.spi_mode = 0;
    ic.pclk_hz = 40000000;
    ic.trans_queue_depth = 1;
    ic.lcd_cmd_bits = 32;
    ic.lcd_param_bits = 8;
    ic.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &ic, &io));
    static const uint8_t z[] = {0}, a20[] = {0x20}, a10[] = {0x10}, aa0[] = {0xa0}, a80[] = {0x80},
                         a55[] = {0x55}, a30[] = {0x30}, aff[] = {0xff}, range[] = {0, 0, 1, 0xdf};
    static const sh8601_lcd_init_cmd_t cmds[] = {
        {0x11, z, 0, 600},   {0xfe, a20, 1, 0},   {0x19, a10, 1, 0}, {0x1c, aa0, 1, 0},
        {0xfe, z, 1, 0},     {0xc4, a80, 1, 0},   {0x3a, a55, 1, 0}, {0x35, z, 1, 0},
        {0x36, a30, 1, 0},   {0x53, a20, 1, 0},   {0x51, aff, 1, 0}, {0x63, aff, 1, 0},
        {0x2a, range, 4, 0}, {0x2b, range, 4, 0}, {0x29, z, 0, 100}};
    sh8601_vendor_config_t vendor{};
    vendor.init_cmds = cmds;
    vendor.init_cmds_size = sizeof cmds / sizeof cmds[0];
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_cfg{};
    panel_cfg.reset_gpio_num = -1;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.vendor_config = &vendor;
    auto &panel = physical_panel;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    esp_lcd_panel_io_i2c_config_t ti = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    ti.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t touch_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c, &ti, &touch_io));
    esp_lcd_touch_config_t tc{};
    tc.x_max = 480;
    tc.y_max = 480;
    tc.rst_gpio_num = GPIO_NUM_11;
    tc.int_gpio_num = GPIO_NUM_5;
    tc.flags.swap_xy = 1;
    tc.flags.mirror_y = 1;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(touch_io, &tc, &touch));
    esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    cfg.task_stack_size = 20 * 1024;
    cfg.task_core_id = 0;
    cfg.stack_in_psram = false;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&cfg));
    rotation_buffer = static_cast<uint16_t *>(
        heap_caps_malloc(StripePixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!rotation_buffer)
        ESP_LOGW("board", "Auto-rotate unavailable: no scratch buffer");
    rotated_panel.draw_bitmap = draw_rotated;
    i2c_device_config_t sensor{};
    sensor.device_address = 0x6b;
    sensor.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    sensor.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(i2c, &sensor, &imu) != ESP_OK)
        ESP_LOGW("board", "Auto-rotate unavailable: sensor bus");
    esp_lv_adapter_display_config_t dc = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
        &rotated_panel, io, 480, 480, ESP_LV_ADAPTER_ROTATE_0);
    dc.profile.buffer_height = 12;
    dc.profile.require_double_buffer = false;
    auto *display = esp_lv_adapter_register_display(&dc);
    assert(display);
    lv_display_add_event_cb(display, round_area, LV_EVENT_INVALIDATE_AREA, nullptr);
    auto *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_read_cb(indev, read_touch);
    brightness(60);
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI("board", "READY CS=15 TP_INT=5 RGB565 stripe=12 heap=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
} // namespace board
