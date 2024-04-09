
void hcsr04_timer_init(void);
void hcsr04_delay_us(uint16_t us);

static void report_distance_task(void);
static void check_distance_task(void);

void nvs0_init(void);
void nvs0_read(void);
void nvs0_set(void);
void update_in_state(void);