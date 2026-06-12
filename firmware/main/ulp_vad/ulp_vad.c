#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "app_config.h"
#include "ulp_vad_shared.h"

RTC_SLOW_ATTR ulp_vad_shared_t ulp_vad_data;

static void check_buttons(void)
{
    uint32_t state = 0;
    if (ulp_riscv_gpio_get_level(BUTTON_UP_GPIO) == 0)     state |= (1 << 0);
    if (ulp_riscv_gpio_get_level(BUTTON_DOWN_GPIO) == 0)   state |= (1 << 1);
    if (ulp_riscv_gpio_get_level(BUTTON_SELECT_GPIO) == 0) state |= (1 << 2);
    if (ulp_riscv_gpio_get_level(BUTTON_BACK_GPIO) == 0)   state |= (1 << 3);
    ulp_vad_data.button_state = state;
}

int main(void)
{
    ulp_riscv_gpio_init(BUTTON_UP_GPIO);
    ulp_riscv_gpio_init(BUTTON_DOWN_GPIO);
    ulp_riscv_gpio_init(BUTTON_SELECT_GPIO);
    ulp_riscv_gpio_init(BUTTON_BACK_GPIO);

    ulp_riscv_gpio_input_enable(BUTTON_UP_GPIO);
    ulp_riscv_gpio_input_enable(BUTTON_DOWN_GPIO);
    ulp_riscv_gpio_input_enable(BUTTON_SELECT_GPIO);
    ulp_riscv_gpio_input_enable(BUTTON_BACK_GPIO);

    ulp_riscv_gpio_pullup_enable(BUTTON_UP_GPIO);
    ulp_riscv_gpio_pullup_enable(BUTTON_DOWN_GPIO);
    ulp_riscv_gpio_pullup_enable(BUTTON_SELECT_GPIO);
    ulp_riscv_gpio_pullup_enable(BUTTON_BACK_GPIO);

    ulp_vad_data.button_state = 0;
    ulp_vad_data.wake_reason = ULP_WAKE_NONE;
    ulp_vad_data.flags = 0;
    ulp_vad_data.vad_ticks = 0;

    ulp_riscv_timer_configure(ULP_VAD_POLL_MS * 1000);

    while (1) {
        check_buttons();

        if (ulp_vad_data.button_state != 0) {
            ulp_vad_data.wake_reason = ULP_WAKE_BUTTON;
            ulp_vad_data.flags |= ULP_FLAG_CPU_WAKE_REQUEST;
            ulp_riscv_wakeup_main_processor();
            ulp_riscv_halt();
        }

        ulp_vad_data.vad_ticks++;
        if (ulp_vad_data.vad_ticks >= ULP_VAD_TICK_INTERVAL) {
            ulp_vad_data.vad_ticks = 0;

            if (ulp_vad_data.flags & ULP_FLAG_VAD_ENABLED) {
                ulp_vad_data.wake_reason = ULP_WAKE_TIMER;
                ulp_vad_data.flags |= ULP_FLAG_CPU_WAKE_REQUEST;
                ulp_riscv_wakeup_main_processor();
                ulp_riscv_halt();
            }
        }

        ulp_riscv_halt();
    }
    return 0;
}
