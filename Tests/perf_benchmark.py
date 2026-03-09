"""
Layer 5 — 性能基准测试
直接通过 TCP JSON-RPC 连接 UnrealAgent 插件，测量各工具的响应延迟。
"""

import asyncio
import json
import time
import sys


class PerfClient:
    def __init__(self, host="127.0.0.1", port=55557):
        self.host = host
        self.port = port
        self.reader = None
        self.writer = None
        self._id = 0

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
        print(f"Connected to {self.host}:{self.port}")

    async def disconnect(self):
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()

    async def call(self, method, params=None):
        self._id += 1
        req = {"jsonrpc": "2.0", "id": self._id, "method": method, "params": params or {}}
        payload = json.dumps(req).encode("utf-8")
        header = f"Content-Length: {len(payload)}\r\n\r\n".encode("utf-8")

        start = time.perf_counter()
        self.writer.write(header + payload)
        await self.writer.drain()

        # 读响应 header
        raw_header = await self.reader.readuntil(b"\r\n\r\n")
        content_length = int(raw_header.decode().split("Content-Length:")[1].strip().split("\r")[0])
        body = await self.reader.readexactly(content_length)
        elapsed_ms = (time.perf_counter() - start) * 1000

        result = json.loads(body.decode("utf-8"))
        ok = "error" not in result
        return elapsed_ms, ok, result


async def bench(client, label, method, params=None, iterations=5):
    """运行多次并返回平均延迟"""
    times = []
    for i in range(iterations):
        ms, ok, _ = await client.call(method, params)
        times.append(ms)
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] {label} #{i+1}: {ms:.1f}ms")
    avg = sum(times) / len(times)
    mn = min(times)
    mx = max(times)
    print(f"  → AVG: {avg:.1f}ms | MIN: {mn:.1f}ms | MAX: {mx:.1f}ms\n")
    return avg


async def main():
    c = PerfClient()
    await c.connect()

    results = {}

    # P-01: get_project_info x5  (阈值 500ms)
    print("=== P-01: get_project_info() x5 [threshold 500ms] ===")
    results["P-01"] = await bench(c, "get_project_info", "get_project_info", iterations=5)

    # P-02: get_world_outliner  (阈值 1000ms)
    print("=== P-02: get_world_outliner() x5 [threshold 1000ms] ===")
    results["P-02"] = await bench(c, "get_world_outliner", "get_world_outliner", iterations=5)

    # P-03: get_blueprint_graph 复杂蓝图  (阈值 2000ms)
    print("=== P-03: get_blueprint_graph() x3 [threshold 2000ms] ===")
    results["P-03"] = await bench(c, "get_blueprint_graph", "get_blueprint_graph",
                                   {"asset_path": "/Game/_TestL4/BP_WF6_Nodes"}, iterations=3)

    # P-04: list_properties  (阈值 500ms)
    print("=== P-04: list_properties() x5 [threshold 500ms] ===")
    results["P-04"] = await bench(c, "list_properties", "list_properties",
                                   {"actor_name": "PointLight"}, iterations=5)

    # P-05: take_screenshot quality=ultra  (阈值 3000ms)
    print("=== P-05: take_screenshot(ultra) x3 [threshold 3000ms] ===")
    results["P-05"] = await bench(c, "screenshot_ultra", "take_screenshot",
                                   {"mode": "scene", "quality": "ultra"}, iterations=3)

    # P-06: take_screenshot quality=low  (阈值 1500ms)
    print("=== P-06: take_screenshot(low) x3 [threshold 1500ms] ===")
    results["P-06"] = await bench(c, "screenshot_low", "take_screenshot",
                                   {"mode": "scene", "quality": "low"}, iterations=3)

    # P-07: set_property x10 连续  (总时间阈值 5000ms)
    print("=== P-07: set_property() x10 sequential [total threshold 5000ms] ===")
    set_times = []
    for i in range(10):
        val = 1.0 + i * 0.5
        ms, ok, _ = await c.call("set_property", {
            "actor_name": "PointLight",
            "property_path": "LightComponent.Intensity",
            "value": val
        })
        set_times.append(ms)
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] set_property #{i+1}: {ms:.1f}ms (value={val})")
    total_p07 = sum(set_times)
    avg_p07 = total_p07 / len(set_times)
    print(f"  → TOTAL: {total_p07:.1f}ms | AVG: {avg_p07:.1f}ms\n")
    results["P-07"] = total_p07

    # P-08: get_output_log(count=200)  (阈值 500ms)
    print("=== P-08: get_output_log(count=200) x3 [threshold 500ms] ===")
    results["P-08"] = await bench(c, "get_output_log", "get_output_log",
                                   {"count": 200}, iterations=3)

    # P-09: get_recent_events(count=200)  (阈值 500ms)
    print("=== P-09: get_recent_events(count=200) x3 [threshold 500ms] ===")
    results["P-09"] = await bench(c, "get_recent_events", "get_recent_events",
                                   {"count": 200}, iterations=3)

    # P-10: compile_blueprint 简单BP  (阈值 3000ms)
    print("=== P-10: compile_blueprint() x3 [threshold 3000ms] ===")
    results["P-10"] = await bench(c, "compile_blueprint", "compile_blueprint",
                                   {"asset_path": "/Game/_TestL4/BP_WF3_Test"}, iterations=3)

    # ========== 汇总 ==========
    thresholds = {
        "P-01": 500, "P-02": 1000, "P-03": 2000, "P-04": 500,
        "P-05": 3000, "P-06": 1500, "P-07": 5000, "P-08": 500,
        "P-09": 500, "P-10": 3000,
    }

    print("\n" + "=" * 60)
    print("Layer 5 Performance Benchmark Summary")
    print("=" * 60)
    print(f"{'ID':<6} {'Actual(ms)':<12} {'Threshold':<12} {'Result':<6}")
    print("-" * 36)
    all_pass = True
    for pid in sorted(results.keys()):
        actual = results[pid]
        threshold = thresholds[pid]
        passed = actual <= threshold
        mark = "PASS" if passed else "FAIL"
        if not passed:
            all_pass = False
        print(f"{pid:<6} {actual:<12.1f} {threshold:<12} {mark}")
    print("-" * 36)
    passed_count = sum(1 for pid in results if results[pid] <= thresholds[pid])
    print(f"Passed: {passed_count}/{len(results)}  {'ALL PASS' if all_pass else 'SOME FAILED'}")

    await c.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
