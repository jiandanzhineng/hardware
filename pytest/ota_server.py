import http.server
import socketserver
import os
import socket

# 配置
PORT = 8080
DIRECTORY = "ota_firmware"

def get_ip_address():
    try:
        # 获取本机IP地址
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        # 设置服务目录
        super().__init__(*args, directory=DIRECTORY, **kwargs)

def run_server():
    # 确保存放固件的目录存在
    if not os.path.exists(DIRECTORY):
        os.makedirs(DIRECTORY)
        print(f"创建目录: {DIRECTORY}")
        # 创建一个测试文件
        with open(os.path.join(DIRECTORY, "firmware_v1.bin"), "w") as f:
            f.write("This is a dummy firmware file for testing.")

    # 允许地址重用，防止重启时报端口占用错误
    socketserver.TCPServer.allow_reuse_address = True
    
    with socketserver.TCPServer(("0.0.0.0", PORT), OTAHandler) as httpd:
        ip = get_ip_address()
        print(f"\n{'='*50}")
        print(f"OTA 服务器运行中...")
        print(f"固件目录: {os.path.abspath(DIRECTORY)}")
        print(f"本地访问: http://127.0.0.1:{PORT}/")
        print(f"局域网访问: http://{ip}:{PORT}/")
        
        print("\n[可用固件列表]")
        files = [f for f in os.listdir(DIRECTORY) if os.path.isfile(os.path.join(DIRECTORY, f))]
        for f in files:
            print(f" - http://{ip}:{PORT}/{f}")
            
        print(f"{'='*50}\n")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n服务器已停止")

if __name__ == "__main__":
    run_server()
