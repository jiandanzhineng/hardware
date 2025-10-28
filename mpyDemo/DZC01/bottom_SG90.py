from machine import Pin, PWM
import time

# 定义IO口
button_pin = 2  # 按键连接到IO6
servo_pin = 7   # 舵机控制信号连接到IO7
led_pin = 10    # LED连接到IO10

# 初始化舵机PWM
servo = PWM(Pin(servo_pin), freq=50)  # 舵机频率通常为50Hz

# 初始化按键和LED
button = Pin(button_pin, Pin.IN, Pin.PULL_UP)
led = Pin(led_pin, Pin.OUT)

# 舵机角度映射函数（角度转脉冲宽度）
def map_angle(angle):
    # 将0-180度映射到500-2500的脉冲宽度（单位：微秒）
    return int((angle / 180) * 2000 + 500)

# 设置舵机角度
def set_servo_angle(angle):
    pulse_width = map_angle(angle)
    # 将微秒转换为占空比（ESP32的PWM占空比范围是0-1023）
    duty = int((pulse_width / 20000) * 1023)
    servo.duty(duty)

# 初始状态：舵机在0度，LED熄灭
current_angle = 0
set_servo_angle(current_angle)
led.value(0)

# 按键状态检测
last_button_state = button.value()
debounce_time = 200  # 消抖时间（毫秒）
last_debounce_time = 0

while True:
    # 读取按键状态
    current_button_state = button.value()
    
    # 检测按键是否按下（下降沿）
    if current_button_state == 0 and last_button_state == 1:
        # 防抖处理
        current_time = time.ticks_ms()
        if current_time - last_debounce_time > debounce_time:
            # 切换舵机角度
            if current_angle == 0:
                current_angle = 180
                led.value(1)  # 舵机转到180度时LED亮起
            else:
                current_angle = 0
                led.value(0)  # 舵机转回0度时LED熄灭
                
            set_servo_angle(current_angle)
            print(f"舵机角度: {current_angle} 度")
        
        last_debounce_time = current_time
    
    last_button_state = current_button_state
    
    # 短暂延时，避免CPU占用过高
    time.sleep_ms(10)