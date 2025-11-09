#!/usr/bin/env python3
"""
使用 network namespace 模拟多客户端压测
"""

import subprocess
import sys
import os
import time
import signal
from concurrent.futures import ThreadPoolExecutor, as_completed

# ==================== 配置 ====================
NUM_CLIENTS = 8
CONCURRENCY_PER_CLIENT = 2000
PER_CLIENT_REQUESTS = 10000
SERVER_IP = "192.168.100.1"
SERVER_PORT = 8080 
TARGET_URL = f"http://{SERVER_IP}:{SERVER_PORT}/"
# =============================================

class NetworkNamespaceManager:
    """管理网络命名空间和虚拟网络"""
    
    def __init__(self, num_clients, server_ip):
        self.num_clients = num_clients
        self.server_ip = server_ip
        self.setup_done = False
        
    def run_cmd(self, cmd, check=True, capture=True, verbose=False):
        """执行命令"""
        if isinstance(cmd, str):
            cmd_list = cmd.split()
        else:
            cmd_list = cmd
            
        try:
            if capture:
                result = subprocess.run(
                    cmd_list,
                    capture_output=True,
                    text=True
                )
            else:
                result = subprocess.run(
                    cmd_list,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL
                )
            
            if verbose or (check and result.returncode != 0):
                if result.returncode != 0:
                    print(f"  命令失败: {' '.join(cmd_list)}")
                    if capture and result.stderr:
                        print(f"  错误: {result.stderr.strip()}")
            
            if check and result.returncode != 0:
                return False, result.stderr if capture else ""
            return True, result.stdout if capture else ""
        except Exception as e:
            if verbose or check:
                print(f"  异常: {e}")
            return False, str(e)
    
    def cleanup(self):
        """清理网络资源"""
        print("\n清理网络资源...")
        for i in range(1, self.num_clients + 1):
            self.run_cmd(f"ip netns del client{i}", check=False, capture=False)
            self.run_cmd(f"ip link del veth{i}", check=False, capture=False)
            self.run_cmd(f"ip link del veth{i}_br", check=False, capture=False)
        self.run_cmd("ip link del br0", check=False, capture=False)
        print("清理完成")
    
    def setup(self):
        """设置网络命名空间和虚拟网桥"""
        print("\n创建虚拟网桥 br0...")
        
        # 创建网桥
        success, _ = self.run_cmd("ip link add br0 type bridge", check=False)
        if success:
            print("✓ 网桥已创建")
        else:
            print("网桥已存在，继续...")
        
        # 配置网桥IP
        success, _ = self.run_cmd(f"ip addr add {self.server_ip}/24 dev br0", check=False)
        if not success:
            print("网桥IP已存在，继续...")
            
        success, err = self.run_cmd("ip link set br0 up", verbose=True)
        if not success:
            print(f"错误: 无法启动网桥: {err}")
            return False
        
        # 创建客户端命名空间
        print(f"创建 {self.num_clients} 个客户端命名空间...")
        success_count = 0
        
        for i in range(1, self.num_clients + 1):
            client_ip = f"192.168.100.{i+1}"
            
            # 创建命名空间
            success, err = self.run_cmd(f"ip netns add client{i}", check=True, verbose=True)
            if not success and "File exists" not in err:
                print(f"✗ 无法创建命名空间 client{i}: {err}")
                continue
            
            # 删除可能存在的旧设备
            self.run_cmd(f"ip link del veth{i}", check=False, capture=False)
            self.run_cmd(f"ip link del veth{i}_br", check=False, capture=False)
            
            # 创建 veth pair
            success, err = self.run_cmd(f"ip link add veth{i} type veth peer name veth{i}_br", verbose=True)
            if not success:
                print(f"✗ 无法创建 veth{i}: {err}")
                continue
            
            # 连接到网桥
            if not self.run_cmd(f"ip link set veth{i}_br master br0")[0]:
                print(f"✗ 无法连接 veth{i}_br 到网桥")
                continue
            self.run_cmd(f"ip link set veth{i}_br up")
            
            # 移入命名空间
            if not self.run_cmd(f"ip link set veth{i} netns client{i}")[0]:
                print(f"✗ 无法移动 veth{i} 到命名空间")
                continue
            
            # 配置命名空间内的网络
            self.run_cmd(f"ip netns exec client{i} ip addr add {client_ip}/24 dev veth{i}")
            self.run_cmd(f"ip netns exec client{i} ip link set veth{i} up")
            self.run_cmd(f"ip netns exec client{i} ip link set lo up")
            self.run_cmd(f"ip netns exec client{i} ip route add default via {self.server_ip}", check=False)
            
            print(f"✓ client{i}: {client_ip}")
            success_count += 1
        
        if success_count == 0:
            print("错误: 没有成功创建任何客户端命名空间")
            return False
        
        if success_count < self.num_clients:
            print(f"警告: 只成功创建了 {success_count}/{self.num_clients} 个客户端")
        
        self.setup_done = True
        return True
    
    def test_connectivity(self, url):
        """测试服务器连接（从第一个客户端命名空间测试）"""
        print("\n测试服务器连接...")
        # 从客户端命名空间内测试，确保路由正确
        result = subprocess.run(
            ["ip", "netns", "exec", "client1", "curl", "-s", "--max-time", "2", url],
            capture_output=True,
            timeout=3
        )
        return result.returncode == 0


class LoadTester:
    """负载测试器"""
    
    def __init__(self, target_url, concurrency, total_requests):
        self.target_url = target_url
        self.concurrency = concurrency
        self.total_requests = total_requests
    
    def run_test_in_namespace(self, client_id):
        """在指定命名空间中运行 ab 测试"""
        cmd = [
            "ip", "netns", "exec", f"client{client_id}",
            "ab",
            "-c", str(self.concurrency),
            "-n", str(self.total_requests),
            "-k",
            self.target_url
        ]
        
        print(f"[client{client_id}] 启动 {self.concurrency} 并发连接...")
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300  # 5分钟超时
            )
            
            if result.returncode == 0:
                return self._parse_result(client_id, result.stdout)
            else:
                return {
                    'client': client_id,
                    'success': False,
                    'error': result.stderr[:200]
                }
        
        except subprocess.TimeoutExpired:
            return {
                'client': client_id,
                'success': False,
                'error': 'Timeout'
            }
        except Exception as e:
            return {
                'client': client_id,
                'success': False,
                'error': str(e)
            }
    
    def _parse_result(self, client_id, output):
        """解析 ab 输出"""
        stats = {
            'client': client_id,
            'success': True
        }
        
        for line in output.split('\n'):
            if "Complete requests:" in line:
                stats['completed'] = int(line.split(':')[1].strip())
            elif "Failed requests:" in line:
                stats['failed'] = int(line.split(':')[1].strip())
            elif "Requests per second:" in line:
                stats['qps'] = float(line.split(':')[1].strip().split('[')[0])
            elif "Time per request:" in line and "mean)" in line:
                stats['latency_mean'] = float(line.split(':')[1].strip().split('[')[0])
        
        return stats


def check_requirements():
    """检查运行要求"""
    # 检查 root 权限
    if os.geteuid() != 0:
        print("错误: 需要 root 权限")
        print("请使用: sudo python3 run_netns_test.py")
        return False
    
    # 检查 ab 命令
    result = subprocess.run(["which", "ab"], capture_output=True)
    if result.returncode != 0:
        print("错误: 未找到 'ab' 命令")
        print("请安装: sudo apt-get install apache2-utils")
        return False
    
    return True


def main():
    # 处理清理参数
    if len(sys.argv) > 1 and sys.argv[1] == "--cleanup":
        manager = NetworkNamespaceManager(10, SERVER_IP)  # 多清理几个
        manager.cleanup()
        return
    
    print("=" * 60)
    print("Network Namespace 多客户端压测")
    print("=" * 60)
    print(f"客户端数量:      {NUM_CLIENTS}")
    print(f"每客户端并发:    {CONCURRENCY_PER_CLIENT}")
    print(f"总并发:          {NUM_CLIENTS * CONCURRENCY_PER_CLIENT}")
    print(f"每客户端请求:    {PER_CLIENT_REQUESTS}")
    print(f"目标URL:         {TARGET_URL}")
    print("=" * 60)
    
    # 检查要求
    if not check_requirements():
        sys.exit(1)
    
    # 设置网络
    manager = NetworkNamespaceManager(NUM_CLIENTS, SERVER_IP)
    
    # 设置信号处理
    def signal_handler(sig, frame):
        print("\n\n收到中断信号...")
        manager.cleanup()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        # 设置网络命名空间
        if not manager.setup():
            print("网络设置失败")
            sys.exit(1)
        
        print(f"\n提示: 服务器需要监听在 0.0.0.0:{SERVER_PORT} 或 {SERVER_IP}:{SERVER_PORT}")
        print("启动命令: ./bin/epoll_server 0.0.0.0")
        
        # 测试连接
        if manager.test_connectivity(TARGET_URL):
            print("✓ 服务器连接正常")
        else:
            print(f"警告: 无法连接到 {TARGET_URL}")
            print("请先启动服务器")
        
        # 运行负载测试
        print("\n" + "=" * 60)
        print("启动并行测试...")
        print("=" * 60)
        
        tester = LoadTester(TARGET_URL, CONCURRENCY_PER_CLIENT, PER_CLIENT_REQUESTS)
        
        start_time = time.time()
        
        with ThreadPoolExecutor(max_workers=NUM_CLIENTS) as executor:
            futures = {
                executor.submit(tester.run_test_in_namespace, i): i
                for i in range(1, NUM_CLIENTS + 1)
            }
            
            results = []
            for future in as_completed(futures):
                result = future.result()
                results.append(result)
                
                if result['success']:
                    print(f"[client{result['client']}] ✓ 完成 - "
                          f"QPS: {result.get('qps', 0):.2f}, "
                          f"延迟: {result.get('latency_mean', 0):.3f}ms, "
                          f"失败: {result.get('failed', 0)}")
                else:
                    print(f"[client{result['client']}] ✗ 失败 - {result.get('error', 'Unknown')}")
        
        total_time = time.time() - start_time
        
        # 汇总结果
        print("\n" + "=" * 60)
        print("测试结果汇总")
        print("=" * 60)
        
        successful = [r for r in results if r['success']]
        
        if successful:
            total_completed = sum(r.get('completed', 0) for r in successful)
            total_failed = sum(r.get('failed', 0) for r in successful)
            total_qps = sum(r.get('qps', 0) for r in successful)
            avg_latency = sum(r.get('latency_mean', 0) for r in successful) / len(successful)
            
            print(f"成功客户端:      {len(successful)}/{NUM_CLIENTS}")
            print(f"总完成请求:      {total_completed:,}")
            print(f"总失败请求:      {total_failed}")
            print(f"总QPS:           {total_qps:.2f} req/s")
            print(f"平均延迟:        {avg_latency:.3f} ms")
            print(f"实际用时:        {total_time:.2f} 秒")
            print(f"实际平均QPS:     {total_completed / total_time:.2f} req/s")
        else:
            print("所有客户端测试失败")
        
        print("=" * 60)
        
        # 自动清理
        manager.cleanup()
    
    except KeyboardInterrupt:
        print("\n\n测试被中断")
        manager.cleanup()
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()
        manager.cleanup()
        sys.exit(1)


if __name__ == "__main__":
    main()
