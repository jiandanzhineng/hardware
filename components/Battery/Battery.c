#include "Battery.h"

const static int Battery_Level_Percent_Table[11] = {3000, 3650, 3700, 3740, 3760, 3795, 3840, 3910, 3980, 4070, 4200};

static int adc_raw;
static int voltage;
static int Bat_voltage;
static bool do_calibration1;

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void adc_calibration_deinit(adc_cali_handle_t handle);
static esp_err_t battery_adc_init(void);
static int toPercentage(int voltage);



/*
 * @description:
 * @return {*}
 */
esp_err_t battery_adc_get_value(uint8_t *VoltagePer)
{
    #ifdef BATTERY_CLOSE_EN
    return ESP_OK;
    #endif
    battery_adc_init();

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &adc_raw));
    ESP_LOGI("battery_adc_get_value", "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, BATTERY_ADC_CHANNEL, adc_raw);
    if (do_calibration1)
    {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
        voltage -= VOLTAGE_COMPENSATION_FACTOR;
        ESP_LOGI("battery_adc_get_value", "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, BATTERY_ADC_CHANNEL, voltage);
        Bat_voltage = (int)((voltage * RESISTANCE_VOLTAGE_DIVIDER));
        ESP_LOGI("battery_adc_get_value", "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, BATTERY_ADC_CHANNEL, Bat_voltage);
        *VoltagePer = toPercentage(Bat_voltage);
        ESP_LOGI("battery_adc_get_value", "ADC%d Channel[%d] battery : %d %% . ", ADC_UNIT_1 + 1, BATTERY_ADC_CHANNEL, (*VoltagePer));
    }

    // Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration1)
    {
        adc_calibration_deinit(adc1_cali_handle);
    }

    gpio_set_level(BATTERY_ADC_EN, 0);

    return ESP_OK;
}
/*
 * @description:
 * @return {*}
 */
static esp_err_t battery_adc_init(void)
{
    #ifdef BATTERY_CLOSE_EN
    return ESP_OK;
    #endif
    
    gpio_set_level(BATTERY_ADC_EN, 1);

    // init adc
    //-------------ADC1 Init---------------//

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_6,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    adc1_cali_handle = NULL;
    do_calibration1 = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_6, &adc1_cali_handle);

    return ESP_OK;
}

/*
 * @description:
 * @param {adc_unit_t} unit
 * @param {adc_atten_t} atten
 * @param {adc_cali_handle_t} *out_handle
 * @return {*}
 */
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    #ifdef BATTERY_CLOSE_EN
    return true;
    #endif
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated)
    {
        ESP_LOGI("adc_calibration_init", "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            // adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        // ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI("adc_calibration_init", "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW("adc_calibration_init", "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE("adc_calibration_init", "Invalid arg or no memory");
    }

    return calibrated;
}

/*
 * @description:
 * @param {adc_cali_handle_t} handle
 * @return {*}
 */
static void adc_calibration_deinit(adc_cali_handle_t handle)
{
    ESP_LOGI("adc_calibration_deinit", "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
    // ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
}

/*
 * @description: 电压值到电池百分比转换函数
 * @param {int} voltage 电压值，单位mV
 * @return {*} 电压百分比
 */
static int toPercentage(int voltage)
{
    int i = 0;
    if (voltage < Battery_Level_Percent_Table[0])
    {
        return 0;
    }

    for (i = 0; i < ARRAY_DIM(Battery_Level_Percent_Table); i++)
    {
        if (voltage < Battery_Level_Percent_Table[i])
        {
            return i * 10 - (10UL * (int)(Battery_Level_Percent_Table[i] - voltage)) / (int)(Battery_Level_Percent_Table[i] - Battery_Level_Percent_Table[i - 1]);
        }
    }

    return 100;
}

/*
100%—-4.20V

90%—–4.06V

80%—–3.98V

70%—–3.92V

60%—3.87V

50%—3.82V

40%—–3.79V

30%—3.77V

20%–3.74V

10%—–3.68V

5%——3.45V

0%—3.00V
*/