'''
HX711 24位A/D转换芯片压力传感器模块，量程为5Kg
'''

from machine import Pin
import utime

class HX711(object):
    
    def __init__(self,sck:int,dt:int,offset=193885.3,calval=249201.5,weight=100,gain=128):
    
        '''
           offset:归零值 去皮净重 秤的自身的重量，
            calval：校准值 获取传感器原始校准物品的原始ADC值，例如：100g砝码的原始值
            weight：校准物品的自身重量单位（g）已知的砝码重量 例如：100g砝码
            gain：根据增益设置脉冲数 gain=128 or 64 or 32
        '''
        self._sck = Pin(sck, Pin.OUT, value=0)
        self._dat = Pin(dt, Pin.IN)
        
        self._offset = offset
        self._calval = calval
        self._weight = weight
        self._gain = gain
        
        if self._gain == 128:
            self.gain_pulses = 1
        elif self._gain == 64:
            self.gain_pulses = 3
        elif self._gain == 32:
            self.gain_pulses = 2
            
    def is_ready(self):
        return  self._dat() == 0
    
    def _read_hx711(self):
        
        count = 0
        while not self.is_ready():
            pass
        
        for i in range(0, 24, 1):
            self._sck(1)
            count = (count << 1) | self._dat()
            self._sck(0)
            
        for _ in range(self.gain_pulses):
            self._sck(1)
            self._sck(0)

        if count & 0x8000000:
            count -= 0x1000000


        return count
    def _filter(self, data) -> float:
        
        Max = 0
        Min = 5000.000      # 5kg
        Summatin = 0
        
        for count in range(0, 10, 1):
            hx711_data = data
            if hx711_data is not None:
                if(hx711_data > Max):
                    Max = hx711_data
                elif hx711_data < Min:
                    Min = hx711_data
                Summatin += hx711_data
            else:
                return 0
        
        Summatin -= Max
        Summatin -= Min
        hx711_filter = Summatin / 8
        
        return (hx711_filter)
    
    def tare(self): #去皮，获取滤波后原始ADC的24位数值
        filter_data = self._filter(self._read_hx711())
        return filter_data
    
    def get_weight(self, modes=True):
        
        data = ((self.tare() - self._offset) * self._weight) / (self._calval - self._offset)
        nums = max(0, min(data, 5000)) #5Kg
        
        return round(nums) if modes else "{0:.1f}".format(nums)
    