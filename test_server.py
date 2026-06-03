import socket
import struct
import threading
import time
import argparse
import statistics
import random
import string
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from typing import List

HOST = "127.0.0.1"
PORT = 8002
TIMEOUT = 5.0


@dataclass
class Result:
    name: str
    passed: bool
    detail: str = ""
    metrics: dict = field(default_factory=dict)

    def __str__(self):
        status = "PASS" if self.passed else "FAIL"
        lines = [f"[{status}] {self.name}"]
        if self.detail:
            lines.append(f"       {self.detail}")
        for k, v in self.metrics.items():
            lines.append(f"       {k}: {v}")
        return "\n".join(lines)


def make_socket() -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(TIMEOUT)
    s.connect((HOST, PORT))
    return s


def _recv_exact(s: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed before full message")
        buf += chunk
    return buf


def send_recv(s: socket.socket, data: bytes) -> bytes:
    # 我们是裸 socket，没有库替我们加头，得自己按服务器协议分帧：
    # [4 字节大端长度头][body]
    s.sendall(struct.pack(">I", len(data)) + data)
    # 回包同样是 [4 字节长度头][body]，先读头拿到长度，再精确读 body
    body_len = struct.unpack(">I", _recv_exact(s, 4))[0]
    return _recv_exact(s, body_len)


def rand_bytes(n: int) -> bytes:
    return "".join(random.choices(string.ascii_letters + string.digits, k=n)).encode()


# ── 1. 正确性 ────────────────────────────────────────────────────────────────

def test_correctness() -> Result:
    cases = [
        ("普通文本",   b"hello world"),
        ("二进制",     bytes(range(256))),
        ("1KB",        rand_bytes(1024)),
        ("64KB 大包",  rand_bytes(65536)),
    ]
    try:
        with make_socket() as s:
            for name, data in cases:
                echo = send_recv(s, data)
                if echo != data:
                    return Result("正确性", False, f"{name}: 收到 {len(echo)} 字节，期望 {len(data)}")
            for _ in range(20):
                msg = rand_bytes(512)
                if send_recv(s, msg) != msg:
                    return Result("正确性", False, "重复发送数据不匹配")
    except Exception as e:
        return Result("正确性", False, str(e))
    return Result("正确性", True, "所有 echo 数据完整匹配")


# ── 2. 并发连接 ──────────────────────────────────────────────────────────────

def _concurrent_worker(worker_id: int, msg_count: int) -> dict:
    errors = 0
    latencies = []
    try:
        with make_socket() as s:
            for _ in range(msg_count):
                data = f"w{worker_id}-".encode() + rand_bytes(32)
                t0 = time.perf_counter()
                echo = send_recv(s, data)
                latencies.append((time.perf_counter() - t0) * 1000)
                if echo != data:
                    errors += 1
    except Exception:
        errors += 1
    return {"errors": errors, "latencies": latencies}


def test_concurrent(num_conns: int = 200, msg_per_conn: int = 50) -> Result:
    t0 = time.time()
    all_latencies = []
    total_errors = 0

    with ThreadPoolExecutor(max_workers=num_conns) as pool:
        futures = [pool.submit(_concurrent_worker, i, msg_per_conn) for i in range(num_conns)]
        for f in as_completed(futures):
            r = f.result()
            total_errors += r["errors"]
            all_latencies.extend(r["latencies"])

    elapsed = time.time() - t0
    total = num_conns * msg_per_conn
    qps = total / elapsed
    p99 = statistics.quantiles(all_latencies, n=100)[98] if len(all_latencies) >= 100 else -1

    metrics = {
        "并发连接数":    num_conns,
        "总消息数":      total,
        "错误数":        total_errors,
        "耗时(s)":       f"{elapsed:.2f}",
        "QPS":           f"{qps:.0f}",
        "avg延迟(ms)":   f"{statistics.mean(all_latencies):.2f}",
        "P99延迟(ms)":   f"{p99:.2f}" if p99 >= 0 else "N/A",
    }
    return Result("并发连接", total_errors == 0, "", metrics)


# ── 3. 吞吐量 ────────────────────────────────────────────────────────────────

def test_throughput(duration: int = 5, msg_size: int = 256) -> Result:
    msg = rand_bytes(msg_size)
    count = 0
    errors = 0
    deadline = time.time() + duration

    try:
        with make_socket() as s:
            while time.time() < deadline:
                try:
                    if send_recv(s, msg) == msg:
                        count += 1
                    else:
                        errors += 1
                except Exception:
                    errors += 1
                    break
    except Exception as e:
        return Result("吞吐量", False, str(e))

    qps = count / duration
    bw = (count * msg_size * 2) / (1024 * 1024) / duration

    metrics = {
        "消息大小(bytes)": msg_size,
        "测试时长(s)":     duration,
        "完成消息数":      count,
        "QPS":             f"{qps:.0f}",
        "带宽(MB/s)":      f"{bw:.2f}",
        "错误数":          errors,
    }
    return Result("吞吐量", errors == 0, "", metrics)


# ── 4. 延迟分布 ──────────────────────────────────────────────────────────────

def test_latency(samples: int = 1000, msg_size: int = 64) -> Result:
    msg = rand_bytes(msg_size)
    latencies = []

    try:
        with make_socket() as s:
            for _ in range(samples):
                t0 = time.perf_counter()
                send_recv(s, msg)
                latencies.append((time.perf_counter() - t0) * 1000)
    except Exception as e:
        return Result("延迟", False, str(e))

    q = statistics.quantiles(latencies, n=100)
    metrics = {
        "采样数":     samples,
        "avg(ms)":    f"{statistics.mean(latencies):.3f}",
        "p50(ms)":    f"{q[49]:.3f}",
        "p90(ms)":    f"{q[89]:.3f}",
        "p99(ms)":    f"{q[98]:.3f}",
        "max(ms)":    f"{max(latencies):.3f}",
    }
    return Result("延迟", True, "", metrics)


# ── 5. 短连接压力 ─────────────────────────────────────────────────────────────

def _stress_worker(_: int) -> int:
    errors = 0
    for _ in range(10):
        try:
            with make_socket() as s:
                msg = rand_bytes(64)
                if send_recv(s, msg) != msg:
                    errors += 1
        except Exception:
            errors += 1
    return errors


def test_stress(num_workers: int = 100) -> Result:
    total_errors = 0
    t0 = time.time()

    with ThreadPoolExecutor(max_workers=num_workers) as pool:
        futures = [pool.submit(_stress_worker, i) for i in range(num_workers)]
        for f in as_completed(futures):
            total_errors += f.result()

    elapsed = time.time() - t0
    total_conns = num_workers * 10
    metrics = {
        "短连接总数":    total_conns,
        "耗时(s)":       f"{elapsed:.2f}",
        "连接速率(/s)":  f"{total_conns / elapsed:.0f}",
        "错误数":        total_errors,
    }
    return Result("短连接压力", total_errors == 0, "", metrics)


# ── 6. 半包 / 慢发 ────────────────────────────────────────────────────────────

def test_partial_send() -> Result:
    # 一帧拆成很多小片、带间隔地发,逼服务端反复走"头不全/体不全则 break 等下次"的拆包分支
    body = rand_bytes(2000)
    frame = struct.pack(">I", len(body)) + body
    try:
        with make_socket() as s:
            # 4 字节长度头逐字节发,打"头不全"分支
            for i in range(4):
                s.sendall(frame[i:i + 1])
                time.sleep(0.005)
            # body 分小块发,打"体不全"分支
            off = 4
            while off < len(frame):
                s.sendall(frame[off:off + 256])
                off += 256
                time.sleep(0.003)
            blen = struct.unpack(">I", _recv_exact(s, 4))[0]
            if _recv_exact(s, blen) != body:
                return Result("半包慢发", False, "echo 与原文不符")
    except Exception as e:
        return Result("半包慢发", False, str(e))
    return Result("半包慢发", True, "逐字节/分块发送下服务端拆包正确")


# ── 7. 超大长度头拒绝 ─────────────────────────────────────────────────────────

def test_oversized_header() -> Result:
    # 发一个声称长度 > MAX_MESSAGE_SIZE(16MB) 的头,服务端应识别并主动断开,而不是傻等 body 或 OOM
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        s.connect((HOST, PORT))
        s.sendall(struct.pack(">I", 16 * 1024 * 1024 + 1))  # 只发头,不发 body
        s.settimeout(2.0)
        data = s.recv(16)   # 服务端关闭→返回 b""
        s.close()
        if data:
            return Result("超大头拒绝", False, f"服务端没断开,反而回了 {len(data)} 字节")
    except socket.timeout:
        return Result("超大头拒绝", False, "服务端既没关也没回,超时(可能在傻等 body)")
    except Exception as e:
        return Result("超大头拒绝", False, str(e))
    return Result("超大头拒绝", True, "服务端识别超大长度头并主动断开")


# ── 8. 半关闭:积压数据发完再关 ───────────────────────────────────────────────

def test_half_close_drain() -> Result:
    # 打"对端 FIN 时服务端 _send_buf 还有积压"这条延迟关闭路径,验证服务端把积压全发完才关、一字节不丢。
    # 手法:猛发大量数据但「先不读回包」,逼服务端发送缓冲积压;再 shutdown(SHUT_WR) 发 FIN;
    #      然后才开始读,读到 EOF,核对收到的字节和帧内容跟发出去的完全一致。
    N = 200
    bodies = [rand_bytes(64 * 1024) for _ in range(N)]
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        s.connect((HOST, PORT))
        for body in bodies:                                  # 1) 只发不收,撑起服务端 _send_buf
            s.sendall(struct.pack(">I", len(body)) + body)
        s.shutdown(socket.SHUT_WR)                            # 2) 半关闭:发 FIN,但还要继续收
        received = b""
        while True:                                          # 3) 现在才读,直到服务端发完并关闭
            chunk = s.recv(65536)
            if not chunk:
                break
            received += chunk
        s.close()
    except Exception as e:
        return Result("半关闭drain", False, str(e))

    expected = sum(4 + len(b) for b in bodies)
    if len(received) != expected:                            # 4) 先看字节总数,少了就是被提前关截断
        return Result("半关闭drain", False,
                      f"字节数不符:收到 {len(received)},期望 {expected}(疑被提前关闭截断)")
    off = 0                                                  # 再逐帧核对内容与顺序
    for i, body in enumerate(bodies):
        blen = struct.unpack(">I", received[off:off + 4])[0]
        off += 4
        if blen != len(body) or received[off:off + blen] != body:
            return Result("半关闭drain", False, f"第 {i} 帧内容不匹配")
        off += blen
    return Result("半关闭drain", True, f"积压 {N} 帧 / {expected} 字节全部发完且完整,无丢失")


# ── 9. RST(异常断开)服务端存活 ──────────────────────────────────────────────

def test_rst() -> Result:
    # 客户端用 SO_LINGER=0 强制 close 发 RST(而非 FIN),服务端 recv 拿 ECONNRESET,
    # 应正常关闭该连接而不崩溃;用一条新连接验证服务端仍存活。
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        s.connect((HOST, PORT))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        s.sendall(struct.pack(">I", 100) + b"x" * 100)
        s.close()                                            # 触发 RST
        time.sleep(0.1)
        with make_socket() as s2:                            # 新连接仍能正常 echo = 服务端没崩
            msg = rand_bytes(128)
            if send_recv(s2, msg) != msg:
                return Result("RST存活", False, "RST 后服务端 echo 异常")
    except Exception as e:
        return Result("RST存活", False, str(e))
    return Result("RST存活", True, "客户端 RST 后服务端存活、新连接正常")


# ── 入口 ──────────────────────────────────────────────────────────────────────

TEST_MAP = {
    "correctness": test_correctness,
    "concurrent":  lambda: test_concurrent(200, 50),
    "throughput":  lambda: test_throughput(5, 256),
    "latency":     lambda: test_latency(1000),
    "stress":      lambda: test_stress(100),
    "partial":     test_partial_send,
    "oversized":   test_oversized_header,
    "halfclose":   test_half_close_drain,
    "rst":         test_rst,
}

def main():
    global HOST, PORT
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", choices=list(TEST_MAP.keys()) + ["all"], default="all")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    HOST = args.host
    PORT = args.port

    print(f"目标: {HOST}:{PORT}\n{'=' * 50}")

    tests = TEST_MAP if args.test == "all" else {args.test: TEST_MAP[args.test]}
    results: List[Result] = []

    for name, fn in tests.items():
        print(f">>> {name}")
        r = fn()
        results.append(r)
        print(r, "\n")

    passed = sum(1 for r in results if r.passed)
    print(f"{'=' * 50}\n结果: {passed}/{len(results)} 通过")


if __name__ == "__main__":
    main()
