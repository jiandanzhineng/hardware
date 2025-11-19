#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
虚拟设备GUI控制器
基于tkinter的图形界面，用于控制和监控虚拟IoT设备
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import threading
import time
import json
import sys
import os
from typing import Dict, Any, Optional
import logging

# 添加上级目录到Python路径，以便导入virtual_devices
parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)

from virtual_devices import TD01Device, DianjiDevice, ZidongsuoDevice, QTZDevice, QiyaDevice, BaseVirtualDevice

class DeviceGUIController:
    """设备GUI控制器主类"""
    
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("IoT虚拟设备控制面板")
        self.root.geometry("1200x800")
        
        # 设备实例字典
        self.devices: Dict[str, BaseVirtualDevice] = {}
        
        # GUI组件字典
        self.device_frames: Dict[str, tk.Frame] = {}
        self.property_widgets: Dict[str, Dict[str, tk.Widget]] = {}
        
        # 日志显示
        self.setup_logging()
        
        # 创建主界面
        self.create_main_interface()
        
        # 自动创建并启动所有设备类型
        self.auto_create_devices()
        
        # 状态更新线程
        self.update_thread = None
        self.running = False
    
    def setup_logging(self):
        """设置日志系统"""
        # 创建自定义日志处理器，将日志输出到GUI
        self.log_handler = GUILogHandler()
        
        # 配置根日志记录器
        logging.getLogger().addHandler(self.log_handler)
        logging.getLogger().setLevel(logging.INFO)
    
    def create_main_interface(self):
        """创建主界面"""
        # 创建主框架
        main_frame = ttk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # 顶部控制区域
        control_frame = ttk.LabelFrame(main_frame, text="设备控制", padding=10)
        control_frame.pack(fill=tk.X, pady=(0, 10))
        
        # 设备创建按钮
        ttk.Button(control_frame, text="创建TD01设备", 
                  command=lambda: self.create_device("TD01")).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="创建电击设备", 
                  command=lambda: self.create_device("DIANJI")).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="创建自动锁设备", 
                  command=lambda: self.create_device("ZIDONGSUO")).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="创建QTZ设备", 
                  command=lambda: self.create_device("QTZ")).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="创建气压传感器", 
                  command=lambda: self.create_device("QIYA")).pack(side=tk.LEFT, padx=5)
        
        # 分隔符
        ttk.Separator(control_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        
        # 全局控制按钮
        ttk.Button(control_frame, text="启动所有设备", 
                  command=self.start_all_devices).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="停止所有设备", 
                  command=self.stop_all_devices).pack(side=tk.LEFT, padx=5)
        
        # 创建主要内容区域
        content_frame = ttk.Frame(main_frame)
        content_frame.pack(fill=tk.BOTH, expand=True)
        
        # 左侧设备控制区域
        devices_frame = ttk.LabelFrame(content_frame, text="设备列表", padding=10)
        devices_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))
        
        # 设备列表滚动区域
        self.devices_canvas = tk.Canvas(devices_frame)
        devices_scrollbar = ttk.Scrollbar(devices_frame, orient="vertical", command=self.devices_canvas.yview)
        self.devices_scrollable_frame = ttk.Frame(self.devices_canvas)
        
        # 创建双栏布局容器
        self.devices_columns_frame = ttk.Frame(self.devices_scrollable_frame)
        self.devices_columns_frame.pack(fill=tk.BOTH, expand=True)
        
        # 左栏和右栏
        self.left_column = ttk.Frame(self.devices_columns_frame)
        self.left_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))
        
        self.right_column = ttk.Frame(self.devices_columns_frame)
        self.right_column.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(5, 0))
        
        # 设备计数器，用于交替放置到左右栏
        self.device_count = 0
        
        self.devices_scrollable_frame.bind(
            "<Configure>",
            lambda e: self.devices_canvas.configure(scrollregion=self.devices_canvas.bbox("all"))
        )
        
        self.devices_canvas.create_window((0, 0), window=self.devices_scrollable_frame, anchor="nw")
        self.devices_canvas.configure(yscrollcommand=devices_scrollbar.set)
        
        self.devices_canvas.pack(side="left", fill="both", expand=True)
        devices_scrollbar.pack(side="right", fill="y")
        
        # 右侧日志区域
        log_frame = ttk.LabelFrame(content_frame, text="系统日志", padding=10)
        log_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=False, padx=(5, 0))
        
        # 日志文本区域
        self.log_text = scrolledtext.ScrolledText(log_frame, width=50, height=30, 
                                                 font=("Consolas", 9))
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        # 将日志处理器连接到文本组件
        self.log_handler.set_text_widget(self.log_text)
        
        # 日志控制按钮
        log_control_frame = ttk.Frame(log_frame)
        log_control_frame.pack(fill=tk.X, pady=(5, 0))
        
        ttk.Button(log_control_frame, text="清空日志", 
                  command=self.clear_log).pack(side=tk.LEFT, padx=5)
        ttk.Button(log_control_frame, text="保存日志", 
                  command=self.save_log).pack(side=tk.LEFT, padx=5)
    
    def auto_create_devices(self):
        """自动创建并启动所有设备类型"""
        device_types = ["TD01", "DIANJI", "ZIDONGSUO", "QTZ", "QIYA"]
        
        for device_type in device_types:
            try:
                # 创建设备（初始化阶段不立即启动，避免重复）
                self.create_device(device_type, auto_start=False)
                
                # 获取刚创建的设备ID
                device_ids = [d for d in self.devices.keys() if d.startswith(device_type.lower())]
                if device_ids:
                    latest_device_id = max(device_ids)  # 获取最新创建的设备ID
                    
                    # 延迟启动设备，确保GUI完全初始化
                    self.root.after(1000, lambda did=latest_device_id: self.start_device(did))
                    
                    logging.info(f"已自动创建并计划启动设备: {device_type} - {latest_device_id}")
                    
            except Exception as e:
                logging.error(f"自动创建设备 {device_type} 失败: {str(e)}")
    
    def create_device(self, device_type: str, auto_start: bool = True):
        """创建新设备"""
        # 生成设备ID
        device_count = len([d for d in self.devices.keys() if d.startswith(device_type.lower())])
        device_id = f"{device_type.lower()}{device_count + 1:03d}aabbcc"
        
        try:
            # 创建设备实例
            if device_type == "TD01":
                device = TD01Device(device_id)
            elif device_type == "DIANJI":
                device = DianjiDevice(device_id)
            elif device_type == "ZIDONGSUO":
                device = ZidongsuoDevice(device_id)
            elif device_type == "QTZ":
                device = QTZDevice(device_id)
            elif device_type == "QIYA":
                device = QiyaDevice(device_id)
            else:
                messagebox.showerror("错误", f"未知设备类型: {device_type}")
                return
            
            # 添加到设备字典
            self.devices[device_id] = device
            
            # 创建设备控制界面
            self.create_device_frame(device_id, device)

            # 新建设备后自动启动
            if auto_start:
                self.root.after(300, lambda did=device_id: self.start_device(did))
            
            logging.info(f"创建了新设备: {device_type} - {device_id}")
            
        except Exception as e:
            messagebox.showerror("错误", f"创建设备失败: {str(e)}")
            logging.error(f"创建设备失败: {str(e)}")
    
    def create_device_frame(self, device_id: str, device: BaseVirtualDevice):
        """为设备创建控制界面"""
        # 选择放置的栏（左栏或右栏）
        parent_column = self.left_column if self.device_count % 2 == 0 else self.right_column
        self.device_count += 1
        
        # 创建设备框架
        device_frame = ttk.LabelFrame(parent_column, 
                                     text=f"{device.device_type} - {device_id}", 
                                     padding=8)
        device_frame.pack(fill=tk.X, pady=3)
        
        self.device_frames[device_id] = device_frame
        self.property_widgets[device_id] = {}
        
        # 设备状态控制
        status_frame = ttk.Frame(device_frame)
        status_frame.pack(fill=tk.X, pady=(0, 10))
        
        # 启动/停止按钮
        start_btn = ttk.Button(status_frame, text="启动", 
                              command=lambda: self.start_device(device_id))
        start_btn.pack(side=tk.LEFT, padx=5)
        
        stop_btn = ttk.Button(status_frame, text="停止", 
                             command=lambda: self.stop_device(device_id))
        stop_btn.pack(side=tk.LEFT, padx=5)
        
        # 删除按钮
        delete_btn = ttk.Button(status_frame, text="删除", 
                               command=lambda: self.delete_device(device_id))
        delete_btn.pack(side=tk.RIGHT, padx=5)
        
        # 状态指示器
        status_label = ttk.Label(status_frame, text="状态: 已停止", foreground="red")
        status_label.pack(side=tk.RIGHT, padx=10)
        self.property_widgets[device_id]['status_label'] = status_label
        
        # 属性控制区域
        props_frame = ttk.LabelFrame(device_frame, text="设备属性", padding=3)
        props_frame.pack(fill=tk.X, pady=(0, 5))
        
        # 为每个属性创建控制组件
        row = 0
        # 需要在GUI中隐藏的属性
        hidden_props = {"device_type", "sleep_time"}
        for prop_name, prop_info in device.properties.items():
            # 跳过隐藏属性
            if prop_name in hidden_props:
                continue
            if prop_info["readable"]:
                # 属性标签
                ttk.Label(props_frame, text=f"{prop_name}:").grid(row=row, column=0, sticky="w", padx=3, pady=1)
                
                # 属性值显示
                value_var = tk.StringVar(value=str(prop_info["value"]))
                value_label = ttk.Label(props_frame, textvariable=value_var, relief="sunken", width=12)
                value_label.grid(row=row, column=1, padx=3, pady=1)
                
                self.property_widgets[device_id][f"{prop_name}_var"] = value_var
                
                # 如果属性可写，添加控制组件
                if prop_info["writeable"]:
                    if prop_name in ["power", "voltage", "delay", "low_band", "high_band", "report_delay_ms", "sleep_time", "report_interval", "pressure"]:
                        # 数值输入
                        entry = ttk.Entry(props_frame, width=8)
                        entry.grid(row=row, column=2, padx=3, pady=1)
                        
                        set_btn = ttk.Button(props_frame, text="设置", 
                                           command=lambda p=prop_name, e=entry: self.set_property(device_id, p, e.get()))
                        set_btn.grid(row=row, column=3, padx=3, pady=1)
                        
                    elif prop_name in ["shock", "open"]:
                        # 开关按钮
                        toggle_btn = ttk.Button(props_frame, text="切换", 
                                              command=lambda p=prop_name: self.toggle_property(device_id, p))
                        toggle_btn.grid(row=row, column=2, padx=3, pady=1)
                
                row += 1
        
        # 设备特定的动作按钮
        actions_frame = ttk.LabelFrame(device_frame, text="设备动作", padding=3)
        actions_frame.pack(fill=tk.X)
        
        if device.device_type == "TD01":
            ttk.Button(actions_frame, text="模拟按键点击", 
                      command=lambda: self.send_action(device_id, "key_boot_clicked")).pack(side=tk.LEFT, padx=3)
        
        elif device.device_type == "ZIDONGSUO":
            ttk.Button(actions_frame, text="模拟按键点击", 
                      command=lambda: self.send_action(device_id, "key_clicked")).pack(side=tk.LEFT, padx=3)
        
        elif device.device_type == "QTZ":
            ttk.Button(actions_frame, text="按钮0按下", 
                      command=lambda: self.simulate_button_press(device_id, "button0")).pack(side=tk.LEFT, padx=3)
            ttk.Button(actions_frame, text="按钮1按下", 
                      command=lambda: self.simulate_button_press(device_id, "button1")).pack(side=tk.LEFT, padx=3)
            
            # 距离模拟
            distance_frame = ttk.Frame(actions_frame)
            distance_frame.pack(side=tk.LEFT, padx=5)
            ttk.Label(distance_frame, text="模拟距离:").pack(side=tk.LEFT)
            distance_entry = ttk.Entry(distance_frame, width=6)
            distance_entry.pack(side=tk.LEFT, padx=1)
            ttk.Button(distance_frame, text="设置", 
                      command=lambda: self.set_distance(device_id, distance_entry.get())).pack(side=tk.LEFT, padx=1)

    
    def start_device(self, device_id: str):
        """启动设备"""
        if device_id in self.devices:
            # 在后台线程中启动设备，避免阻塞GUI
            def start_device_thread():
                try:
                    self.devices[device_id].start()
                    # 在主线程中更新GUI
                    self.root.after(0, lambda: self.property_widgets[device_id]['status_label'].config(
                        text="状态: 运行中", foreground="green"))
                    logging.info(f"设备 {device_id} 已启动")
                except Exception as e:
                    # 在主线程中显示错误
                    self.root.after(0, lambda: messagebox.showerror("错误", f"启动设备失败: {str(e)}"))
                    logging.error(f"启动设备 {device_id} 失败: {str(e)}")
            
            # 立即更新状态为"启动中"
            self.property_widgets[device_id]['status_label'].config(text="状态: 启动中...", foreground="orange")
            
            # 在后台线程中启动设备
            threading.Thread(target=start_device_thread, daemon=True).start()
    
    def stop_device(self, device_id: str):
        """停止设备"""
        if device_id in self.devices:
            try:
                self.devices[device_id].stop()
                self.property_widgets[device_id]['status_label'].config(text="状态: 已停止", foreground="red")
                logging.info(f"设备 {device_id} 已停止")
            except Exception as e:
                messagebox.showerror("错误", f"停止设备失败: {str(e)}")
                logging.error(f"停止设备 {device_id} 失败: {str(e)}")
    
    def delete_device(self, device_id: str):
        """删除设备"""
        if device_id in self.devices:
            # 先停止设备
            self.stop_device(device_id)
            
            # 删除GUI组件
            if device_id in self.device_frames:
                self.device_frames[device_id].destroy()
                del self.device_frames[device_id]
            
            if device_id in self.property_widgets:
                del self.property_widgets[device_id]
            
            # 删除设备实例
            del self.devices[device_id]
            
            # 更新设备计数器
            self.device_count = max(0, self.device_count - 1)
            
            logging.info(f"设备 {device_id} 已删除")
    
    def start_all_devices(self):
        """启动所有设备"""
        for device_id in self.devices.keys():
            self.start_device(device_id)
    
    def stop_all_devices(self):
        """停止所有设备"""
        device_ids = list(self.devices.keys())  # 创建副本避免迭代时修改
        
        for device_id in device_ids:
            try:
                logging.info(f"[GUI] 正在停止设备 {device_id} ...")
                if device_id in self.devices:
                    # 使用线程停止设备，避免阻塞
                    def stop_device_with_timeout(dev_id):
                        try:
                            self.devices[dev_id].stop()
                            logging.info(f"[GUI] 设备 {dev_id} stop() 返回")
                        except Exception as e:
                            logging.error(f"[GUI] 停止设备 {dev_id} 时发生错误: {e}")
                    
                    stop_thread = threading.Thread(target=stop_device_with_timeout, args=(device_id,), daemon=True)
                    t0 = time.perf_counter()
                    logging.info(f"[GUI] 创建停止线程并等待 (≤2s): {device_id}")
                    stop_thread.start()
                    stop_thread.join(timeout=2)  # 2秒超时
                    
                    if stop_thread.is_alive():
                        logging.warning(f"[GUI] 设备 {device_id} 停止超时 (等待 {time.perf_counter()-t0:.3f}s)")
                    else:
                        logging.info(f"[GUI] 设备 {device_id} 停止完成，用时 {time.perf_counter()-t0:.3f}s")
                    
                    # 更新GUI状态
                    if device_id in self.property_widgets and 'status_label' in self.property_widgets[device_id]:
                        self.property_widgets[device_id]['status_label'].config(text="状态: 已停止", foreground="red")
                        
            except Exception as e:
                logging.error(f"[GUI] 停止设备 {device_id} 失败: {e}")
        
        logging.info("[GUI] 所有设备停止操作完成")
    
    def set_property(self, device_id: str, prop_name: str, value: str):
        """设置设备属性"""
        if device_id in self.devices:
            try:
                # 类型转换
                if prop_name in ["power", "voltage", "delay", "low_band", "high_band", "report_delay_ms", "sleep_time"]:
                    value = int(float(value))
                elif prop_name in ["pressure"]:
                    value = float(value)
                
                # 更新设备属性
                device = self.devices[device_id]
                if prop_name in device.properties and device.properties[prop_name]["writeable"]:
                    device.properties[prop_name]["value"] = value
                    device._on_property_changed(prop_name, value, 0)
                    
                    # 更新GUI显示
                    if f"{prop_name}_var" in self.property_widgets[device_id]:
                        self.property_widgets[device_id][f"{prop_name}_var"].set(str(value))
                    
                    logging.info(f"设备 {device_id} 属性 {prop_name} 设置为 {value}")
                
            except ValueError:
                messagebox.showerror("错误", "请输入有效的数值")
            except Exception as e:
                messagebox.showerror("错误", f"设置属性失败: {str(e)}")
                logging.error(f"设置设备 {device_id} 属性 {prop_name} 失败: {str(e)}")
    
    def toggle_property(self, device_id: str, prop_name: str):
        """切换布尔属性"""
        if device_id in self.devices:
            device = self.devices[device_id]
            if prop_name in device.properties:
                current_value = device.properties[prop_name]["value"]
                new_value = 1 if current_value == 0 else 0
                
                device.properties[prop_name]["value"] = new_value
                device._on_property_changed(prop_name, new_value, 0)
                
                # 更新GUI显示
                if f"{prop_name}_var" in self.property_widgets[device_id]:
                    self.property_widgets[device_id][f"{prop_name}_var"].set(str(new_value))
                
                logging.info(f"设备 {device_id} 属性 {prop_name} 切换为 {new_value}")
    
    def send_action(self, device_id: str, action: str):
        """发送动作"""
        if device_id in self.devices:
            self.devices[device_id].send_action(action)
            logging.info(f"设备 {device_id} 执行动作: {action}")
    
    def simulate_button_press(self, device_id: str, button_name: str):
        """模拟按钮按下"""
        if device_id in self.devices:
            device = self.devices[device_id]
            
            # 按钮按下
            device.properties[button_name]["value"] = 1
            if f"{button_name}_var" in self.property_widgets[device_id]:
                self.property_widgets[device_id][f"{button_name}_var"].set("1")
            
            # 调用属性变化处理和发送消息
            device._on_property_changed(button_name, 1, 0)
            device._send_property_response(button_name, 0)
            
            logging.info(f"设备 {device_id} {button_name} 按下")
            
            # 1秒后释放
            def release_button():
                time.sleep(1)
                device.properties[button_name]["value"] = 0
                if f"{button_name}_var" in self.property_widgets[device_id]:
                    self.property_widgets[device_id][f"{button_name}_var"].set("0")
                
                # 调用属性变化处理和发送消息
                device._on_property_changed(button_name, 0, 0)
                device._send_property_response(button_name, 0)
                
                logging.info(f"设备 {device_id} {button_name} 释放")
            
            threading.Thread(target=release_button, daemon=True).start()
    
    def set_distance(self, device_id: str, distance_str: str):
        """设置QTZ设备距离"""
        if device_id in self.devices:
            try:
                distance = float(distance_str)
                device = self.devices[device_id]
                if hasattr(device, 'current_distance'):
                    device.current_distance = distance
                    device.properties["distance"]["value"] = distance
                    
                    # 更新GUI显示
                    if "distance_var" in self.property_widgets[device_id]:
                        self.property_widgets[device_id]["distance_var"].set(str(distance))
                    
                    logging.info(f"设备 {device_id} 距离设置为 {distance}mm")
                
            except ValueError:
                messagebox.showerror("错误", "请输入有效的距离值")
            except Exception as e:
                messagebox.showerror("错误", f"设置距离失败: {str(e)}")
    
    def clear_log(self):
        """清空日志"""
        self.log_text.delete(1.0, tk.END)
    
    def save_log(self):
        """保存日志"""
        from tkinter import filedialog
        filename = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("文本文件", "*.txt"), ("所有文件", "*.*")]
        )
        if filename:
            try:
                with open(filename, 'w', encoding='utf-8') as f:
                    f.write(self.log_text.get(1.0, tk.END))
                messagebox.showinfo("成功", "日志已保存")
            except Exception as e:
                messagebox.showerror("错误", f"保存日志失败: {str(e)}")
    
    def start_update_thread(self):
        """启动状态更新线程"""
        self.running = True
        self.update_thread = threading.Thread(target=self._update_properties, daemon=True)
        self.update_thread.start()
    
    def _update_properties(self):
        """定期更新设备属性显示"""
        while self.running:
            try:
                for device_id, device in self.devices.items():
                    if device.running and device_id in self.property_widgets:
                        # 更新所有可读属性
                        for prop_name, prop_info in device.properties.items():
                            if prop_info["readable"] and f"{prop_name}_var" in self.property_widgets[device_id]:
                                current_value = str(prop_info["value"])
                                self.property_widgets[device_id][f"{prop_name}_var"].set(current_value)
                
                time.sleep(1)  # 每秒更新一次
                
            except Exception as e:
                logging.error(f"更新属性显示失败: {str(e)}")
                time.sleep(1)
    
    def run(self):
        """运行GUI应用"""
        # 启动状态更新线程
        self.start_update_thread()
        
        # 设置关闭事件处理
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
        # 启动主循环
        self.root.mainloop()
    
    def on_closing(self):
        """关闭应用时的清理工作"""
        logging.info("[GUI] 开始关闭应用...")
        
        # 强制关闭窗口
        try:
            logging.info("[GUI] 调用 root.quit() ...")
            self.root.quit()
            logging.info("[GUI] 调用 root.destroy() ...")
            self.root.destroy()
            logging.info("[GUI] 窗口销毁完成")
        except Exception as e:
            logging.error(f"[GUI] 窗口销毁失败: {str(e)}")


class GUILogHandler(logging.Handler):
    """自定义日志处理器，将日志输出到GUI文本组件"""
    
    def __init__(self):
        super().__init__()
        self.text_widget = None
        self.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s'))
    
    def set_text_widget(self, text_widget):
        """设置文本组件"""
        self.text_widget = text_widget
    
    def emit(self, record):
        """输出日志记录"""
        if self.text_widget:
            try:
                msg = self.format(record) + '\n'
                # 在主线程中更新GUI
                self.text_widget.after(0, lambda: self._append_text(msg))
            except Exception:
                pass
    
    def _append_text(self, msg):
        """添加文本到组件"""
        if self.text_widget:
            self.text_widget.insert(tk.END, msg)
            self.text_widget.see(tk.END)
            
            # 限制日志行数，避免内存占用过多
            lines = self.text_widget.get(1.0, tk.END).split('\n')
            if len(lines) > 1000:
                # 删除前面的行
                self.text_widget.delete(1.0, f"{len(lines) - 800}.0")


if __name__ == "__main__":
    # 创建并运行GUI控制器
    controller = DeviceGUIController()
    controller.run()
