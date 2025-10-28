from machine import Pin
from hx711 import HX711
import time

hx = HX711(sck=0, dt=1, offset=352703.000,calval=249201.5,weight=100,gain=128 )

def weight_module():
#     data = hx.tare() #获取原始ADC值，填入offset，获取校准值后填入calval
#     print("weight: %f"%(data))
    
#     data = hx.get_weight(modes=1) #获取整数值
#     print("weight: %f"%(data))
    
    data = hx.get_weight(modes=0) #获取浮点数值，带小数
    print("weight: %s"%(data))
    
    time.sleep(0.1)
    
    
def main():
    
    while True:
        weight_module()
        
        pass
    
if __name__ == "__main__":
    main()