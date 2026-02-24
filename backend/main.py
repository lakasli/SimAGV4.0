from __future__ import annotations
from pathlib import Path
import os
import subprocess
import shutil
from typing import List, Optional, Any, Dict
import threading
from datetime import datetime

from fastapi import FastAPI, HTTPException, Body
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from typing import Union
from .schemas import AGVRegistration, RegisterRequest, RegisterResponse, UnregisterResponse, AGVInfo, StaticConfigPatch, DynamicConfigUpdate, StatusResponse
from .schemas import SimSettingsPatch
from .schemas import TranslateRequest, RotateRequest
from .schemas import SwitchMapRequest
from .schemas import VisualizationEventReport
from .agv_manager import AGVManager
from .sim_process_manager import SimulatorProcessManager
from fastapi import WebSocket
import asyncio
from .websocket_manager import WebSocketManager
try:
    from SimVehicleSys.sim_vehicle.action_executor import execute_translation_movement, execute_rotation_movement  # type: ignore
except Exception:  # pragma: no cover
    execute_translation_movement = None  # type: ignore
    execute_rotation_movement = None  # type: ignore
try:
    from SimVehicleSys.config.settings import get_config  # type: ignore
except Exception:  # pragma: no cover
    get_config = None  # type: ignore
from .mqtt_bridge import MQTTBridge
from .mqtt_publisher import MqttPublisher
import json
try:
    from SimVehicleSys.protocol.vda_2_0_0.order import Order as VDAOrder  # type: ignore
except Exception:  # pragma: no cover
    VDAOrder = None  # type: ignore
from .instance_file_store import InstanceFileStore

project_root = Path(__file__).resolve().parents[1]
app = FastAPI(title="SimAGV Backend", version="0.1.0")


# CORS for local dev
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

if execute_translation_movement is None:
    async def execute_translation_movement(serial_number: str, dx: float, dy: float, agv_manager: AGVManager, ws_manager: WebSocketManager, movement_state: str | None = None):  # type: ignore[no-redef]
        rt = agv_manager.move_translate(serial_number, dx, dy, movement_state)
        if not rt:
            return None
        try:
            await ws_manager.broadcast_json({"type": "agv_runtime", "serial_number": serial_number, "runtime": rt.model_dump()})
        except Exception:
            pass
        return rt

if execute_rotation_movement is None:
    async def execute_rotation_movement(serial_number: str, dtheta: float, agv_manager: AGVManager, ws_manager: WebSocketManager):  # type: ignore[no-redef]
        rt = agv_manager.move_rotate(serial_number, dtheta)
        if not rt:
            return None
        try:
            await ws_manager.broadcast_json({"type": "agv_runtime", "serial_number": serial_number, "runtime": rt.model_dump()})
        except Exception:
            pass
        return rt

# Managers
agv_storage = project_root / "backend" / "data" / "registered_agvs.json"
agv_manager = AGVManager(agv_storage)
ws_manager = WebSocketManager()
sim_manager = SimulatorProcessManager(project_root)
instance_root_dir = project_root / "Simulation_vehicle_example"
instance_store = InstanceFileStore(
    instances_root=instance_root_dir,
    template_config_yaml=project_root / "SimAGV" / "config.yaml",
    template_binary=project_root / "SimAGV" / "build" / "SimAGV",
)
_build_lock = threading.Lock()

def _build_runtime_binary() -> Path:
    with _build_lock:
        build_script = project_root / "SimAGV" / "build.sh"
        if not build_script.exists():
            raise RuntimeError("build script missing")
        proc = subprocess.run(
            ["bash", str(build_script), "--name", "SimAGV"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or "").strip()
            if len(detail) > 2000:
                detail = detail[-2000:]
            raise RuntimeError(detail or "build failed")
        ok_path: Path | None = None
        try:
            merged = "\n".join([str(proc.stdout or ""), str(proc.stderr or "")])
            for line in reversed(merged.splitlines()):
                s = str(line or "").strip()
                if s.startswith("OK "):
                    cand = s[3:].strip()
                    if cand:
                        ok_path = Path(cand)
                        break
        except Exception:
            ok_path = None
        if ok_path is not None and ok_path.exists() and ok_path.is_file():
            return ok_path
        built_bin = project_root / "SimAGV" / "build" / "SimAGV"
        if built_bin.exists() and built_bin.is_file():
            return built_bin
        legacy_bin = project_root / "SimAGV" / "build" / "AMB-01"
        if legacy_bin.exists() and legacy_bin.is_file():
            return legacy_bin
        raise RuntimeError("build output missing")

def _has_instance_binary(serial_number: str) -> bool:
    try:
        fp = instance_store.binary_path(serial_number)
        if fp.exists() and fp.is_file():
            return True
        return False
    except Exception:
        return False

def _ensure_instance_binary(serial_number: str) -> bool:
    if _has_instance_binary(serial_number):
        return False
    try:
        inst_dir = instance_store.instance_dir(serial_number)
        legacy = inst_dir / "AMB-01"
        dst = instance_store.binary_path(serial_number)
        if legacy.exists() and legacy.is_file():
            try:
                os.replace(legacy, dst)
            except Exception:
                shutil.copy2(legacy, dst)
                try:
                    legacy.unlink(missing_ok=True)
                except Exception:
                    pass
            try:
                dst.chmod(dst.stat().st_mode | 0o111)
            except Exception:
                pass
            return False
    except Exception:
        pass
    try:
        instance_store.sync_binary(serial_number, overwrite=True)
        if _has_instance_binary(serial_number):
            return False
    except Exception:
        pass
    built_bin = _build_runtime_binary()
    instance_store.sync_binary_from(serial_number, built_bin, overwrite=True)
    return True

# Static mounts: frontend and maps
frontend_dir = project_root / "frontend"
maps_dir = project_root / "maps"
shelf_dir = project_root / "SimAGV" / "shelf"
shelf_dir_legacy = project_root / "SimVehicleSys" / "shelf"
simagv_dir = project_root / "SimAGV"
if frontend_dir.exists():
    app.mount("/static", StaticFiles(directory=str(frontend_dir)), name="static")
if maps_dir.exists():
    app.mount("/maps", StaticFiles(directory=str(maps_dir)), name="maps")
if not shelf_dir.exists() and shelf_dir_legacy.exists():
    shelf_dir = shelf_dir_legacy
if shelf_dir.exists():
    # 公开托盘/货架模型静态文件
    app.mount("/shelf", StaticFiles(directory=str(shelf_dir)), name="shelf")
if simagv_dir.exists():
    app.mount("/simagv", StaticFiles(directory=str(simagv_dir)), name="simagv")

# --- PM2 管理与 ecosystem 生成 ---
#

try:
    import shutil as _shutil
    _py = _shutil.which("python3")
    if _py:
        PYTHON_BIN = _py
    else:
        cand = Path.home() / "miniconda3" / "bin" / "python3"
        PYTHON_BIN = str(cand if cand.exists() else Path("/usr/bin/python3"))
except Exception:
    PYTHON_BIN = str(Path("/usr/bin/python3"))

PM2_MANAGE_SCRIPT = project_root / "pm2_manage.py"

def _pm2_manage_run(args: list[str]) -> tuple[int, str, str]:
    try:
        proc = subprocess.run(
            [PYTHON_BIN, str(PM2_MANAGE_SCRIPT)] + list(args),
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=False,
        )
        return proc.returncode, proc.stdout, proc.stderr
    except Exception as e:
        return 1, "", f"pm2_manage run failed: {e}"

def _pm2_direct_run(args: list[str]) -> tuple[int, str, str]:
    try:
        proc = subprocess.run(
            ["pm2"] + list(args),
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=False,
        )
        return proc.returncode, proc.stdout, proc.stderr
    except Exception as e:
        return 1, "", f"pm2 run failed: {e}"

def _pm2_run(args: list[str]) -> tuple[int, str, str]:
    try:
        if not args:
            return 1, "", "pm2 args empty"
        cmd = str(args[0])
        runner = _pm2_manage_run if PM2_MANAGE_SCRIPT.exists() else _pm2_direct_run
        if cmd == "jlist":
            return runner(["jlist"])
        if cmd == "start":
            eco = str(args[1]) if len(args) > 1 else str(project_root / "ecosystem.config.js")
            only_val = ""
            if "--only" in args:
                i = args.index("--only")
                if i + 1 < len(args):
                    only_val = str(args[i + 1])
            if PM2_MANAGE_SCRIPT.exists():
                run_args = ["sync-and-start", "--ecosystem", eco]
                if only_val:
                    run_args += ["--only", only_val]
                return _pm2_manage_run(run_args)
            start_args = ["start", eco]
            if only_val:
                start_args += ["--only", only_val]
            rc, out, err = _pm2_direct_run(start_args)
            if rc == 0:
                try:
                    _pm2_direct_run(["save"])
                except Exception:
                    pass
            return rc, out, err
        if cmd == "stop":
            name = str(args[1]) if len(args) > 1 else ""
            if not name:
                return 1, "", "pm2 stop missing name"
            if PM2_MANAGE_SCRIPT.exists():
                return _pm2_manage_run(["stop", "--name", name])
            return _pm2_direct_run(["stop", name])
        if cmd == "delete":
            name = str(args[1]) if len(args) > 1 else ""
            if not name:
                return 1, "", "pm2 delete missing name"
            if PM2_MANAGE_SCRIPT.exists():
                return _pm2_manage_run(["delete", "--name", name])
            return _pm2_direct_run(["delete", name])
        return 1, "", f"unsupported pm2 command: {cmd}"
    except Exception as e:
        return 1, "", f"pm2 run failed: {e}"

def _pm2_jlist() -> list[Dict[str, Any]]:
    code, out, _err = _pm2_run(["jlist"])
    if code != 0 or not out:
        return []
    try:
        text = str(out)
        if not text.lstrip().startswith("["):
            l = text.find("[")
            r = text.rfind("]")
            if l != -1 and r != -1 and r > l:
                text = text[l : r + 1]
        data = json.loads(text)
        if isinstance(data, list):
            return data
        return []
    except Exception:
        return []

def _pm2_namespace_summary(apps: list[Dict[str, Any]], namespace: str) -> Dict[str, Any]:
    total_cpu = 0.0
    total_mem = 0
    count = 0
    for proc in apps:
        try:
            env = proc.get("pm2_env") or {}
            if str(env.get("namespace") or "") != namespace:
                continue
            monit = proc.get("monit") or {}
            cpu = float(monit.get("cpu") or 0)
            mem = int(monit.get("memory") or 0)
            total_cpu += cpu
            total_mem += mem
            count += 1
        except Exception:
            continue
    return {"cpu_percent": total_cpu, "memory_bytes": total_mem, "process_count": count}

def _get_mem_total_bytes() -> int:
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    parts = line.split()
                    if len(parts) >= 2:
                        kb = int(parts[1])
                        return kb * 1024
    except Exception:
        pass
    return 0

def _pm2_namespace_processes(apps: list[Dict[str, Any]], namespace: str, mem_total_bytes: int) -> list[Dict[str, Any]]:
    out: list[Dict[str, Any]] = []
    for proc in apps:
        try:
            env = proc.get("pm2_env") or {}
            ns = str(env.get("namespace") or "")
            if ns != namespace:
                args_probe = str(env.get("args") or "")
                exec_path = str(env.get("pm_exec_path") or "")
                pm_cwd = str(env.get("pm_cwd") or "")
                if namespace == "simagv":
                    path_probe = (exec_path + " " + pm_cwd).replace("\\", "/")
                    if "Simulation_vehicle_example/" not in path_probe and "SimVehicleSys.main" not in args_probe:
                        continue
                elif namespace == "equip":
                    if "SimEquipment." not in args_probe:
                        continue
                else:
                    continue
            monit = proc.get("monit") or {}
            cpu = float(monit.get("cpu") or 0.0)
            mem = int(monit.get("memory") or 0)
            pid = int(proc.get("pid") or 0)
            name = str(proc.get("name") or "")
            mem_percent = (float(mem) * 100.0 / float(mem_total_bytes)) if mem_total_bytes > 0 else 0.0
            out.append(
                {
                    "name": name,
                    "pid": pid,
                    "cpu_percent": cpu,
                    "memory_bytes": mem,
                    "memory_percent": mem_percent,
                }
            )
        except Exception:
            continue
    return out

def _ps_list_processes(mem_total_bytes: int) -> tuple[list[Dict[str, Any]], list[Dict[str, Any]]]:
    import re
    import os

    agv_out: list[Dict[str, Any]] = []
    equip_out: list[Dict[str, Any]] = []
    known_serials: set[str] = set()
    try:
        for info in agv_manager.list_agvs() or []:
            sn = str(getattr(info, "serial_number", "") or "").strip()
            if sn:
                known_serials.add(sn)
    except Exception:
        known_serials = set()
    try:
        proc = subprocess.run(
            ["ps", "-eo", "pid,pcpu,rss,args", "--no-headers"],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0 or not proc.stdout:
            return agv_out, equip_out
        serial_re = re.compile(r"--serial\s+([A-Za-z0-9_-]+)")
        equip_re = re.compile(r"SimEquipment\.([A-Za-z0-9_-]+)")
        for line in proc.stdout.splitlines():
            parts = line.strip().split(None, 3)
            if len(parts) < 4:
                continue
            pid_raw, cpu_raw, rss_kb_raw, args = parts[0], parts[1], parts[2], parts[3]
            try:
                pid = int(pid_raw)
                cpu = float(cpu_raw)
                rss_kb = int(float(rss_kb_raw))
                mem = rss_kb * 1024
            except Exception:
                continue
            mem_percent = (float(mem) * 100.0 / float(mem_total_bytes)) if mem_total_bytes > 0 else 0.0
            if "SimVehicleSys.main" in args and "--serial" in args:
                m = serial_re.search(args)
                name = m.group(1) if m else ""
                if name:
                    agv_out.append(
                        {
                            "name": name,
                            "pid": pid,
                            "cpu_percent": cpu,
                            "memory_bytes": mem,
                            "memory_percent": mem_percent,
                        }
                    )
                continue
            if known_serials:
                cmd0 = args.strip().split(None, 1)[0] if args.strip() else ""
                base = os.path.basename(cmd0)
                if base in known_serials:
                    agv_out.append(
                        {
                            "name": base,
                            "pid": pid,
                            "cpu_percent": cpu,
                            "memory_bytes": mem,
                            "memory_percent": mem_percent,
                        }
                    )
                    continue
            if "SimEquipment." in args:
                m = equip_re.search(args)
                mod = m.group(1) if m else ""
                if mod:
                    equip_out.append(
                        {
                            "name": f"equip-{mod}",
                            "pid": pid,
                            "cpu_percent": cpu,
                            "memory_bytes": mem,
                            "memory_percent": mem_percent,
                        }
                    )
                continue
    except Exception:
        return agv_out, equip_out
    return agv_out, equip_out

def _summarize_processes(processes: list[Dict[str, Any]]) -> Dict[str, Any]:
    cpu_total = 0.0
    mem_total = 0
    for p in processes:
        try:
            cpu_total += float(p.get("cpu_percent") or 0.0)
            mem_total += int(p.get("memory_bytes") or 0)
        except Exception:
            continue
    return {"cpu_percent": cpu_total, "memory_bytes": mem_total, "process_count": len(processes), "processes": processes}

def _collect_perf_snapshot() -> Dict[str, Any]:
    mem_total_bytes = _get_mem_total_bytes()
    apps = _pm2_jlist()
    if apps:
        agv_processes = _pm2_namespace_processes(apps, "simagv", mem_total_bytes)
        equip_processes = _pm2_namespace_processes(apps, "equip", mem_total_bytes)
        if agv_processes or equip_processes:
            return {
                "source": "pm2",
                "mem_total_bytes": mem_total_bytes,
                "simagv": _summarize_processes(agv_processes),
                "equip": _summarize_processes(equip_processes),
            }
    agv_processes, equip_processes = _ps_list_processes(mem_total_bytes)
    return {
        "source": "ps",
        "mem_total_bytes": mem_total_bytes,
        "simagv": _summarize_processes(agv_processes),
        "equip": _summarize_processes(equip_processes),
    }

def _safe_name(sn: str) -> str:
    s = str(sn)
    out = []
    for c in s:
        if c.isalnum() or c in ("-", "_"):
            out.append(c)
        else:
            out.append("-")
    return "".join(out)

def _canonicalize_map_id(raw: str | None) -> str:
    s = str(raw or "").replace("\\", "/").strip()
    if not s:
        return ""
    if "/" in s:
        s = s.split("/")[-1] or s
    if s.lower().endswith(".scene"):
        s = s[: -len(".scene")]
    return s

def _render_agv_block(sn: str) -> str:
    safe = _safe_name(sn)
    inst_dir = project_root / "Simulation_vehicle_example" / safe
    return (
        "{\n"
        f"  name: \"{safe}\",\n"
        f"  cwd: \"{str(inst_dir)}\",\n"
        f"  script: \"./{safe}\",\n"
        "  exec_interpreter: \"none\",\n"
        "  namespace: \"simagv\",\n"
        "  autorestart: true,\n"
        "  watch: false,\n"
        "  max_restarts: 10,\n"
        "  restart_delay: 2000,\n"
        f"  error_file: \"{str(inst_dir / 'error.log')}\",\n"
        f"  out_file: \"{str(inst_dir / 'out.log')}\",\n"
        "  max_size: \"20M\"\n"
        "}"
    )

def _render_equip_block(dir_name: str) -> str:
    safe = _safe_name(dir_name)
    return (
        "{\n"
        f"  name: \"equip-{safe}\",\n"
        f"  cwd: \"{str(project_root)}\",\n"
        f"  script: \"{PYTHON_BIN}\",\n"
        f"  args: \"-m SimEquipment.{safe}\",\n"
        "  namespace: \"equip\",\n"
        "  env: { PYTHONUNBUFFERED: \"1\" },\n"
        "  autorestart: true,\n"
        "  watch: false,\n"
        "  max_restarts: 10,\n"
        "  restart_delay: 2000\n"
        "}"
    )

def _discover_equip_dirs() -> list[str]:
    eq_dir = project_root / "SimEquipment"
    names: list[str] = []
    try:
        if eq_dir.exists():
            for p in eq_dir.iterdir():
                if p.is_dir() and (p / "__main__.py").exists():
                    names.append(p.name)
    except Exception:
        pass
    return names

def _write_ecosystem_files(agv_serials: list[str]) -> None:
    agv_serials = [str(x).strip() for x in agv_serials if str(x).strip()]
    eco_fp = project_root / "ecosystem.config.js"
    equip_dirs = _discover_equip_dirs()
    blocks: list[str] = []
    for sn in agv_serials:
        blocks.append(_render_agv_block(sn))
    for d in equip_dirs:
        blocks.append(_render_equip_block(d))
    body = (
        "module.exports = {\n"
        "  apps: [\n"
        + ",\n".join("    " + b.replace("\n", "\n    ") for b in blocks)
        + ("\n" if blocks else "")
        + "  ]\n"
        "};\n"
    )
    eco_fp.write_text(body, encoding="utf-8")

@app.get("/api/equipments")
def list_equipments() -> List[dict]:
    equipments_dir = project_root / "SimEquipment"
    items: List[dict] = []
    try:
        if equipments_dir.exists():
            for p in equipments_dir.iterdir():
                try:
                    if not p.is_dir():
                        continue
                    cfg_fp = p / "config.json"
                    if not cfg_fp.exists():
                        continue
                    data = json.loads(cfg_fp.read_text(encoding="utf-8")) or {}
                    mqtt = data.get("mqtt", {}) or {}
                    t = data.get("type")
                    if not t:
                        nm = p.name.lower()
                        if "door" in nm:
                            t = "door"
                        elif "caller" in nm:
                            t = "caller"
                        elif "elevator" in nm:
                            t = "elevator"
                        elif "light" in nm:
                            t = "light"
                        else:
                            t = "device"
                    site_raw = data.get("site")
                    if isinstance(site_raw, list):
                        site_list = [str(x) for x in site_raw if x is not None]
                    elif isinstance(site_raw, str):
                        site_list = [site_raw]
                    else:
                        site_list = []
                    if str(t).lower() != "door" and len(site_list) > 1:
                        site_list = site_list[:1]
                    items.append({
                        "name": data.get("name"),
                        "type": t,
                        "manufacturer": data.get("manufacturer"),
                        "serial_number": data.get("serial_number"),
                        "vda_version": data.get("vda_version"),
                        "vda_full_version": data.get("vda_full_version"),
                        "ip": data.get("ip"),
                        "site": site_list,
                        "map_id": data.get("map_id"),
                        "state_frequency": data.get("state_frequency"),
                        "action_time": data.get("action_time"),
                        "trigger_mode": data.get("trigger_mode"),
                        "mqtt": {
                            "host": mqtt.get("host"),
                            "port": mqtt.get("port"),
                            "vda_interface": mqtt.get("vda_interface"),
                        },
                        "dir_name": p.name,
                    })
                except Exception:
                    pass
    except Exception:
        items = []
    return items

@app.post("/api/equipments/{dir_name}/instant")
def publish_equipment_instant(dir_name: str, body: dict = Body(...)):
    equipments = list_equipments()
    eq = None
    for it in equipments:
        if str(it.get("dir_name")) == str(dir_name):
            eq = it
            break
    if not eq:
        raise HTTPException(status_code=404, detail="Equipment not found")
    publisher = getattr(app.state, "mqtt_publisher", None)
    if not publisher:
        raise HTTPException(status_code=500, detail="MQTT publisher not available")
    try:
        # 直接透传 body（应为 camelCase InstantActions 结构，如 docs/仿真设备instanceaction.md）
        publisher.publish_equipment_actions(eq, body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish equipment actions failed: {e}")
    return {"published": True, "dir_name": dir_name}

@app.get("/api/equipments/{dir_name}/config")
def get_equipment_config(dir_name: str) -> dict:
    equipments_dir = project_root / "SimEquipment"
    target = equipments_dir / dir_name / "config.json"
    if not target.exists():
        raise HTTPException(status_code=404, detail="Equipment config not found")
    try:
        return json.loads(target.read_text(encoding="utf-8"))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Read config failed: {e}")

@app.post("/api/equipments/{dir_name}/config")
def update_equipment_config(dir_name: str, body: dict = Body(...)):
    equipments_dir = project_root / "SimEquipment"
    target = equipments_dir / dir_name / "config.json"
    if not target.exists():
        raise HTTPException(status_code=404, detail="Equipment config not found")
    try:
        current = json.loads(target.read_text(encoding="utf-8"))
        # 递归合并更新
        def deep_update(d: dict, u: dict):
            for k, v in u.items():
                if isinstance(v, dict) and k in d and isinstance(d[k], dict):
                    deep_update(d[k], v)
                else:
                    d[k] = v
        deep_update(current, body)
        # 写入文件
        target.write_text(json.dumps(current, indent=2, ensure_ascii=False), encoding="utf-8")
        # 尝试重启设备服务
        # 这里假设设备服务会在配置变更后自动重启或需要手动重启
        # 目前 SimAGV 的设备是独立进程，这里仅更新文件，用户可能需要手动重启设备服务
        return current
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Update config failed: {e}")

@app.get("/api/maps")
def list_maps() -> List[str]:
    vehicle_dir = maps_dir / "VehicleMap"
    if not vehicle_dir.exists():
        return []
    files: List[str] = []
    for p in vehicle_dir.iterdir():
        if p.is_file() and p.suffix.lower() == ".scene":
            files.append(f"VehicleMap/{p.name}")
    return files

@app.get("/api/maps/{kind}")
def list_maps_by_kind(kind: str) -> List[str]:
    # 仅支持列出 VehicleMap 或 ViewerMap 下的 .scene 文件
    if kind not in {"VehicleMap", "ViewerMap"}:
        raise HTTPException(status_code=404, detail=f"Map kind '{kind}' not found")
    target = maps_dir / kind
    if not target.exists() or not target.is_dir():
        raise HTTPException(status_code=404, detail=f"Map kind '{kind}' not found")
    files: List[str] = []
    for p in target.iterdir():
        if p.is_file() and p.suffix.lower() == ".scene":
            files.append(f"{kind}/{p.name}")
    return files

@app.get("/")
def index():
    index_file = frontend_dir / "index.html"
    if not index_file.exists():
        raise HTTPException(status_code=404, detail="Frontend not found")
    return FileResponse(index_file)

# 前端配置：提供轮询间隔（毫秒）与中心前移偏移量（米）
@app.get("/api/config")
def get_frontend_config():
    try:
        # 优先返回运行态覆盖值，其次回退到默认配置
        runtime_val = getattr(app.state, "frontend_poll_interval_ms", None)
        if runtime_val is not None:
            interval = int(runtime_val)
        else:
            cfg = get_config()
            interval = int(getattr(cfg.settings, "frontend_poll_interval_ms", 100))
        # 安全边界：至少 10ms，至多 5000ms
        interval = max(10, min(5000, interval))
        # 中心前移偏移量（米）：已废弃
        offset = 0.0
        return {"polling_interval_ms": interval, "center_forward_offset_m": offset}
    except Exception:
        # 失败时回退到 100ms
        return {"polling_interval_ms": 100, "center_forward_offset_m": 0.1}

@app.get("/api/perf/summary")
def get_perf_summary():
    snap = _collect_perf_snapshot()
    sim = snap.get("simagv") or {}
    equip = snap.get("equip") or {}
    return {
        "source": snap.get("source"),
        "mem_total_bytes": snap.get("mem_total_bytes"),
        "simagv": sim,
        "equip": equip,
    }


@app.post("/api/agv/{serial_number}/visualization/events")
def post_visualization_events(serial_number: str, body: Union[VisualizationEventReport, List[VisualizationEventReport]]):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    events = body if isinstance(body, list) else [body]
    normalized: List[dict] = []
    for e in events:
        try:
            normalized.append(
                {
                    "serial_number": serial_number,
                    "event_type": str(getattr(e, "event_type", "") or ""),
                    "timestamp_ms": int(getattr(e, "timestamp_ms", 0) or 0),
                    "details": dict(getattr(e, "details", None) or {}),
                }
            )
        except Exception:
            continue
    store = getattr(app.state, "visualization_event_store", None)
    if store is None or not isinstance(store, dict):
        store = {}
        app.state.visualization_event_store = store
    buf = store.get(serial_number)
    if not isinstance(buf, list):
        buf = []
        store[serial_number] = buf
    buf.extend(normalized)
    if len(buf) > 2000:
        store[serial_number] = buf[-2000:]
    return {"received": len(normalized)}

# --- Navigation APIs ---

# 已迁移：站点路径导航改由仿真车内置执行（如需保留，请改为发布 MQTT 指令）

# 旧的导航接口已移除；请改用 /api/sim 路由下的统一接口。

# 路径查询：后端不再维护，若需要可改为读取仿真车发布的可视化数据。

# --- AGV Registry APIs ---
@app.get("/api/agvs", response_model=List[AGVInfo])
def list_agvs():
    agvs = agv_manager.list_agvs()
    # 打印所有已注册机器人实例的 MQTT state 订阅地址（VDA5050 格式）
    try:
        for info in agvs:
            topic = f"uagv/{info.vda_version}/{info.manufacturer}/{info.serial_number}/state"
            print(f"[VDA5050] ID={info.serial_number} 订阅地址: {topic}")
    except Exception as e:
        print(f"[VDA5050] 订阅地址打印失败: {e}")
    return agvs

@app.post("/api/agvs/register", response_model=RegisterResponse)
def register_agvs(body: Union[RegisterRequest, List[AGVRegistration]]):
    regs = body.agvs if isinstance(body, RegisterRequest) else body
    registered, skipped = agv_manager.register_many(regs)
    try:
        reg_by_sn = {str(r.serial_number): r for r in regs}
        for sn in registered:
            r = reg_by_sn.get(str(sn))
            instance_store.ensure_instance_files(
                sn,
                manufacturer=str(getattr(r, "manufacturer", "")) if r else None,
                vda_version=str(getattr(r, "vda_version", "")) if r else None,
            )
    except Exception as e:
        print(f"[InstanceStore] ensure instance files failed: {e}")
    try:
        for sn in registered:
            _ensure_instance_binary(sn)
    except Exception as e:
        try:
            for sn in registered:
                try:
                    instance_store.delete_instance_dir(sn)
                except Exception:
                    pass
                try:
                    agv_manager.unregister(sn)
                except Exception:
                    pass
            _write_ecosystem_files([info.serial_number for info in agv_manager.list_agvs()])
        except Exception:
            pass
        raise HTTPException(status_code=500, detail=f"build failed: {e}")
    try:
        _write_ecosystem_files([info.serial_number for info in agv_manager.list_agvs()])
        names = [_safe_name(sn) for sn in registered]
        if names:
            rc, out, err = _pm2_run(["start", str(project_root / "ecosystem.config.js"), "--only", ",".join(names)])
            if rc != 0:
                raise RuntimeError(err or out or "pm2 start failed")
    except Exception as e:
        try:
            for sn in registered:
                try:
                    _pm2_run(["delete", f"{_safe_name(sn)}"])
                except Exception:
                    pass
                try:
                    instance_store.delete_instance_dir(sn)
                except Exception:
                    pass
                try:
                    agv_manager.unregister(sn)
                except Exception:
                    pass
            _write_ecosystem_files([info.serial_number for info in agv_manager.list_agvs()])
        except Exception:
            pass
        raise HTTPException(status_code=500, detail=f"pm2 start failed: {e}")
    # 注册后立即推送仿真设置并恢复位置（从实例目录下 config.yaml 读取）
    try:
        publisher = getattr(app.state, "mqtt_publisher", None)
        for sn in registered:
            info = agv_manager.get_agv(sn)
            if not info:
                continue
            patch_dict = instance_store.read_sim_settings(sn)
            if patch_dict and publisher:
                try:
                    publisher.publish_sim_settings(info, patch_dict)
                except Exception:
                    pass
            try:
                overrides = getattr(app.state, "sim_settings_by_agv", {})
                if isinstance(overrides, dict):
                    prev = overrides.get(sn, {})
                    if not isinstance(prev, dict):
                        prev = {}
                    prev.update(patch_dict)
                    overrides[sn] = prev
                    app.state.sim_settings_by_agv = overrides
            except Exception:
                pass
            # 恢复地图与位置
            try:
                from .schemas import DynamicConfigUpdate, Position
                cfg = instance_store.load_config_yaml(sn)
                sim_cfg = cfg.get("sim_config") if isinstance(cfg, dict) else {}
                if not isinstance(sim_cfg, dict):
                    sim_cfg = {}
                map_id = sim_cfg.get("map_id")
                pose = instance_store.read_initial_pose(sn) or {}
                px = pose.get("pose_x")
                py = pose.get("pose_y")
                pt = pose.get("pose_theta")
                has_pose = px is not None and py is not None
                dyn = None
                if map_id is not None or px is not None or py is not None or pt is not None:
                    dyn = DynamicConfigUpdate(
                        current_map=str(map_id) if map_id is not None else None,
                        position=Position(
                            x=float(px),
                            y=float(py),
                            theta=float(pt) if pt is not None else None,
                        )
                        if has_pose
                        else None,
                    )
                if dyn:
                    rt = agv_manager.update_dynamic(sn, dyn)
                    if publisher and rt and has_pose:
                        try:
                            publisher.publish_init_position(info, rt)
                        except Exception:
                            pass
            except Exception:
                pass
    except Exception as e:
        print(f"[ConfigStore] post-register restore failed: {e}")
    return RegisterResponse(registered=registered, skipped=skipped)

@app.delete("/api/agvs/{serial_number}", response_model=UnregisterResponse)
def unregister_agv(serial_number: str):
    ok = agv_manager.unregister(serial_number)
    if not ok:
        raise HTTPException(status_code=404, detail="AGV not found")
    try:
        # 同步写入 PM2 ecosystem 并删除对应实例
        _write_ecosystem_files([info.serial_number for info in agv_manager.list_agvs() if info.serial_number != serial_number])
        _pm2_run(["delete", f"{_safe_name(serial_number)}"])
    except Exception as e:
        print(f"[PM2] delete failed: {e}")
    try:
        instance_store.delete_instance_dir(serial_number)
    except Exception as e:
        print(f"[InstanceStore] delete instance dir failed for {serial_number}: {e}")
    return UnregisterResponse(removed=True)

@app.patch("/api/agv/{serial_number}/config/static", response_model=AGVInfo)
def patch_static_config(serial_number: str, body: StaticConfigPatch):
    updated = agv_manager.update_static(serial_number, body)
    if not updated:
        raise HTTPException(status_code=404, detail="AGV not found")
    return updated

@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws_manager.connect(ws)
    try:
        while True:
            await asyncio.sleep(3600)
    except Exception:
        pass
    finally:
        ws_manager.disconnect(ws)

def _start_config_persist_thread() -> None:
    return

@app.on_event("startup")
async def _start_ws_broadcast():
    host = os.getenv("SIMAGV_BACKEND_HOST", "127.0.0.1")
    port = os.getenv("SIMAGV_BACKEND_PORT", "9527")
    print(f"[Web] Listening on http://{host}:{port}")
    # Start MQTT bridge (sink simulator state into AGVManager)
    loop_obj = asyncio.get_running_loop()
    bridge = MQTTBridge(project_root / "SimAGV" / "config.yaml", ws_manager.broadcast_json, loop_obj, agv_manager)
    bridge.start()
    app.state.mqtt_bridge = bridge
    # Initialize MQTT publisher
    publisher = MqttPublisher(project_root / "SimAGV" / "config.yaml")
    publisher.start()
    app.state.mqtt_publisher = publisher
    # 初始化前端轮询间隔覆盖值（用于热更新）
    try:
        cfg = get_config()
        app.state.frontend_poll_interval_ms = int(getattr(cfg.settings, "frontend_poll_interval_ms", 1000))
    except Exception:
        app.state.frontend_poll_interval_ms = 1000
    # 记录每个 AGV 最近一次提交的仿真设置（用于前端预填）
    try:
        app.state.sim_settings_by_agv = {}
    except Exception:
        pass
    try:
        _start_config_persist_thread()
    except Exception:
        pass
    # 仿真车导航由其自身进程负责，后端仅保留 MQTT 桥与发布器
    # 进程管理器：在服务启动时为所有已注册 AGV 确保仿真实例在运行
    try:
        # PM2 模式：仅生成 ecosystem 文件，不再由后端直接拉起仿真车进程
        app.state.sim_manager = sim_manager
        _write_ecosystem_files([info.serial_number for info in agv_manager.list_agvs()])
    except Exception as e:
        print(f"[PM2] ecosystem write on startup failed: {e}")
    # 启动后加载每个实例目录下的配置，恢复仿真设置与离线快照
    try:
        publisher = getattr(app.state, "mqtt_publisher", None)
        overrides = getattr(app.state, "sim_settings_by_agv", {})
        for info in agv_manager.list_agvs():
            try:
                instance_store.ensure_instance_files(info.serial_number, manufacturer=info.manufacturer, vda_version=info.vda_version)
            except Exception:
                pass
            patch_dict = instance_store.read_sim_settings(info.serial_number)
            if patch_dict and publisher:
                try:
                    publisher.publish_sim_settings(info, patch_dict)
                except Exception as e:
                    print(f"[ConfigStore] publish sim settings failed: {e}")
            try:
                prev = overrides.get(info.serial_number, {}) if isinstance(overrides, dict) else {}
                prev.update(patch_dict)
                overrides[info.serial_number] = prev  # type: ignore[index]
                app.state.sim_settings_by_agv = overrides
            except Exception:
                pass
            # 恢复地图与位置
            try:
                from .schemas import DynamicConfigUpdate, Position
                cfg = instance_store.load_config_yaml(info.serial_number)
                sim_cfg = cfg.get("sim_config") if isinstance(cfg, dict) else {}
                if not isinstance(sim_cfg, dict):
                    sim_cfg = {}
                map_id = sim_cfg.get("map_id")
                pose = instance_store.read_initial_pose(info.serial_number) or {}
                px = pose.get("pose_x")
                py = pose.get("pose_y")
                pt = pose.get("pose_theta")
                has_pose = px is not None and py is not None
                if map_id is None and px is None and py is None and pt is None:
                    continue
                dyn = DynamicConfigUpdate(
                    current_map=str(map_id) if map_id is not None else None,
                    position=Position(
                        x=float(px),
                        y=float(py),
                        theta=float(pt) if pt is not None else None,
                    )
                    if has_pose
                    else None,
                )
                rt = agv_manager.update_dynamic(info.serial_number, dyn)
                if publisher and rt:
                    try:
                        map_name = str(rt.current_map or "").strip()
                        if map_name:
                            cx = float(rt.position.x) if has_pose else None
                            cy = float(rt.position.y) if has_pose else None
                            ang = float(rt.position.theta) if (has_pose and rt.position and rt.position.theta is not None) else None
                            publisher.publish_switch_map(
                                info,
                                map_name=map_name,
                                switch_point=None,
                                center_x=cx,
                                center_y=cy,
                                initiate_angle=ang,
                            )
                    except Exception as e:
                        print(f"[InstanceStore] publish switchMap failed: {e}")
            except Exception as e:
                print(f"[InstanceStore] restore runtime failed: {e}")
    except Exception as e:
        print(f"[InstanceStore] startup restore failed: {e}")

@app.on_event("shutdown")
async def _stop_ws_broadcast():
    task = getattr(app.state, "ws_broadcast_task", None)
    if task:
        task.cancel()
    bridge = getattr(app.state, "mqtt_bridge", None)
    if bridge:
        bridge.stop()

@app.post("/api/agv/{serial_number}/config/dynamic", response_model=StatusResponse)
async def post_dynamic_config(serial_number: str, body: DynamicConfigUpdate):
    rt = agv_manager.update_dynamic(serial_number, body)
    if not rt:
        raise HTTPException(status_code=404, detail="AGV not found")
    status = agv_manager.get_status(serial_number)
    if status:
        try:
            publisher = getattr(app.state, "mqtt_publisher", None)
            info = agv_manager.get_agv(serial_number)
            if publisher and info:
                map_name = str(rt.current_map or status.current_map or "").strip()
                if map_name:
                    publisher.publish_switch_map(
                        info,
                        map_name=map_name,
                        switch_point=None,
                        center_x=float(rt.position.x) if rt.position else None,
                        center_y=float(rt.position.y) if rt.position else None,
                        initiate_angle=float(rt.position.theta) if (rt.position and rt.position.theta is not None) else None,
                    )
        except Exception as e:
            print(f"Publish switchMap failed: {e}")
    return status

@app.get("/api/agv/{serial_number}/status", response_model=StatusResponse)
def get_agv_status(serial_number: str):
    status = agv_manager.get_status(serial_number)
    if not status:
        raise HTTPException(status_code=404, detail="AGV not found")
    return status

@app.post("/api/agv/{serial_number}/move/translate", response_model=StatusResponse)
async def move_translate(serial_number: str, body: TranslateRequest):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    dx = float(body.dx)
    dy = float(body.dy)
    dist = (dx * dx + dy * dy) ** 0.5
    if dist <= 1e-6:
        status = agv_manager.get_status(serial_number)
        if not status:
            raise HTTPException(status_code=404, detail="AGV status not found")
        return status
    publisher = getattr(app.state, "mqtt_publisher", None)
    if publisher:
        status = agv_manager.get_status(serial_number)
        theta = 0.0
        if status and status.position and status.position.theta is not None:
            theta = float(status.position.theta)
        import math
        cos_t = math.cos(theta)
        sin_t = math.sin(theta)
        rx = cos_t * dx + sin_t * dy
        ry = -sin_t * dx + cos_t * dy
        desired_dt = 0.05
        speed = max(0.05, min(2.0, dist / max(1e-3, desired_dt)))
        dir_len = (rx * rx + ry * ry) ** 0.5
        if dir_len <= 1e-9:
            vx = -speed if (getattr(body, "movement_state", None) == "forward") else speed
            vy = 0.0
        else:
            vx = (rx / dir_len) * speed
            vy = (ry / dir_len) * speed
        try:
            publisher.publish_translate(info, dist_m=dist, vx_mps=vx, vy_mps=vy)
        except Exception as e:
            print(f"Publish translate failed: {e}")
        if not status:
            raise HTTPException(status_code=404, detail="AGV status not found")
        return status
    rt = await execute_translation_movement(
        serial_number,
        dx,
        dy,
        agv_manager,
        ws_manager,
        getattr(body, "movement_state", None),
    )
    if not rt:
        raise HTTPException(status_code=404, detail="AGV not found")
    status = agv_manager.get_status(serial_number)
    return status

# 热更新仿真设置：发布到 MQTT simConfig，部分更新
@app.post("/api/agv/{serial_number}/sim/settings")
def post_sim_settings(serial_number: str, body: SimSettingsPatch):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    publisher = getattr(app.state, "mqtt_publisher", None)
    # 参数范围校验
    errors: list[str] = []
    def add_err(msg: str):
        errors.append(msg)
    if body.speed is not None and not (0.0 <= float(body.speed) <= 2.0):
        add_err("速度(speed)必须在[0,2]范围内")
    if body.sim_time_scale is not None:
        v = float(body.sim_time_scale)
        if not (v > 0.0 and v <= 10.0):
            add_err("时间缩放(sim_time_scale)必须在(0,10]范围内")
    if body.state_frequency is not None:
        v = int(body.state_frequency)
        if not (v >= 1 and v <= 10):
            add_err("状态频率(state_frequency)必须为[1,10]的正整数")
    if body.visualization_frequency is not None:
        v = int(body.visualization_frequency)
        if not (v >= 1 and v <= 10):
            add_err("可视化频率(visualization_frequency)必须为[1,10]的正整数")
    if body.action_time is not None:
        v = float(body.action_time)
        if not (v >= 1.0 and v <= 10.0):
            add_err("动作时长(action_time)必须在[1,10]范围内")
    if body.frontend_poll_interval_ms is not None:
        v = int(body.frontend_poll_interval_ms)
        if not (v >= 10 and v <= 1000):
            add_err("前端轮询(frontend_poll_interval_ms)必须为[10,1000]的正整数")
    if body.battery_default is not None:
        v = float(body.battery_default)
        if not (v > 0.0 and v <= 100.0):
            add_err("默认电量(battery_default)必须在(0,100]范围内")
    if body.battery_idle_drain_per_min is not None:
        v = float(body.battery_idle_drain_per_min)
        if not (v >= 1.0 and v <= 100.0):
            add_err("空闲耗电(battery_idle_drain_per_min)必须在[1,100]范围内")
    if body.battery_move_empty_multiplier is not None:
        v = float(body.battery_move_empty_multiplier)
        if not (v >= 1.0 and v <= 100.0):
            add_err("空载耗电系数(battery_move_empty_multiplier)必须在[1,100]范围内")
    if body.battery_move_loaded_multiplier is not None:
        v = float(body.battery_move_loaded_multiplier)
        if not (v >= 1.0 and v <= 100.0):
            add_err("载重耗电系数(battery_move_loaded_multiplier)必须在[1,100]范围内")
    if body.battery_charge_per_min is not None:
        v = float(body.battery_charge_per_min)
        if not (v >= 1.0 and v <= 100.0):
            add_err("充电速度(battery_charge_per_min)必须在[1,100]范围内")
    if errors:
        raise HTTPException(status_code=400, detail="; ".join(errors))
    try:
        # 雷达/安全参数范围校验
        if body.radar_fov_deg is not None:
            v = float(body.radar_fov_deg)
            if not (1.0 <= v <= 360.0):
                add_err("雷达FOV(radar_fov_deg)必须在[1,360]范围内")
        if body.radar_radius_m is not None:
            v = float(body.radar_radius_m)
            if not (0.01 <= v <= 10.0):
                add_err("雷达半径(radar_radius_m)必须在[0.01,10]范围内")
        if body.safety_scale is not None:
            v = float(body.safety_scale)
            if not (1.0 <= v <= 5.0):
                add_err("安全范围系数(safety_scale)必须在[1.0,5.0]范围内")
        if errors:
            raise HTTPException(status_code=400, detail="; ".join(errors))
        # 先更新后端运行态的前端轮询覆盖值（若提交了该项）
        if body.frontend_poll_interval_ms is not None:
            try:
                app.state.frontend_poll_interval_ms = int(body.frontend_poll_interval_ms)
            except Exception:
                pass
        # 记录最近一次设置（用于前端预填）
        try:
            current = getattr(app.state, "sim_settings_by_agv", None)
            if current is None:
                app.state.sim_settings_by_agv = {}
                current = app.state.sim_settings_by_agv
            update = {k: v for k, v in body.model_dump().items() if v is not None}
            prev = current.get(serial_number, {})
            prev.update(update)
            current[serial_number] = prev
        except Exception:
            pass
        try:
            instance_store.ensure_instance_files(serial_number, manufacturer=info.manufacturer, vda_version=info.vda_version)
        except Exception:
            pass
        if publisher:
            publisher.publish_sim_settings(info, body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish sim settings failed: {e}")
    return {"updated": True, "serial_number": serial_number}

# 获取当前仿真设置：合并默认配置与最近一次提交的覆盖值
@app.get("/api/agv/{serial_number}/sim/settings")
def get_sim_settings(serial_number: str):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    try:
        instance_store.ensure_instance_files(serial_number, manufacturer=info.manufacturer, vda_version=info.vda_version)
    except Exception:
        pass
    base = instance_store.read_sim_settings(serial_number)
    overrides = getattr(app.state, "sim_settings_by_agv", {})
    ov = overrides.get(serial_number, {}) if isinstance(overrides, dict) else {}
    def pick(name: str, default):
        if isinstance(ov, dict) and name in ov and ov.get(name) is not None:
            return ov.get(name)
        if name in base and base.get(name) is not None:
            return base.get(name)
        return default
    def pick_prefer_base(name: str, default):
        if name in base and base.get(name) is not None:
            return base.get(name)
        if isinstance(ov, dict) and name in ov and ov.get(name) is not None:
            return ov.get(name)
        return default
    fp_runtime = getattr(app.state, "frontend_poll_interval_ms", 1000)
    result = {
        "action_time": pick("action_time", 1.0),
        "speed": pick("speed", 1.0),
        # 物理参数（用于前端物理参数弹窗预填与仿真约束）
        "speed_min": pick("speed_min", 0.01),
        "speed_max": pick("speed_max", 2.0),
        "acceleration_max": pick("acceleration_max", 2.0),
        "deceleration_max": pick("deceleration_max", 2.0),
        "height_min": pick("height_min", 0.01),
        "height_max": pick("height_max", 0.10),
        "width": pick("width", 0.745),
        "length": pick("length", 1.03),
        "state_frequency": pick("state_frequency", 10),
        "visualization_frequency": pick("visualization_frequency", 1),
        "map_id": pick_prefer_base("map_id", "default"),
        "sim_time_scale": pick("sim_time_scale", 1.0),
        "battery_default": pick("battery_default", 100.0),
        "battery_idle_drain_per_min": pick("battery_idle_drain_per_min", 1.0),
        "battery_move_empty_multiplier": pick("battery_move_empty_multiplier", 1.5),
        "battery_move_loaded_multiplier": pick("battery_move_loaded_multiplier", 2.5),
        "battery_charge_per_min": pick("battery_charge_per_min", 10.0),
        "frontend_poll_interval_ms": int(fp_runtime),
        "radar_fov_deg": pick("radar_fov_deg", 60.0),
        "radar_radius_m": pick("radar_radius_m", 0.5),
        "safety_scale": pick("safety_scale", 1.1),
    }
    return result

# --- PM2 控制接口（按实例启动/停止） ---

@app.post("/api/agvs/{serial}/pm2/start")
def pm2_start_agv(serial: str):
    try:
        info = agv_manager.get_agv(serial)
        instance_store.ensure_instance_files(
            serial,
            manufacturer=str(getattr(info, "manufacturer", "")) if info else None,
            vda_version=str(getattr(info, "vda_version", "")) if info else None,
        )
        built = _ensure_instance_binary(serial)
        _write_ecosystem_files([a.serial_number for a in agv_manager.list_agvs()])
        rc, out, err = _pm2_run(["start", str(project_root / "ecosystem.config.js"), "--only", f"{_safe_name(serial)}"])
        if rc != 0:
            raise HTTPException(status_code=500, detail=err or out or "pm2 start failed")
        return {"started": True, "serial": serial, "built": bool(built)}
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/agvs/{serial}/pm2/start/stream")
def pm2_start_agv_stream(serial: str):
    def sse_pack(event_name: str, data_obj: Dict[str, Any]) -> str:
        return f"event: {event_name}\ndata: {json.dumps(data_obj, ensure_ascii=False)}\n\n"

    def stream_iter():
        safe = _safe_name(serial)
        yield sse_pack("status", {"serial": serial, "phase": "prepare"})
        try:
            info = agv_manager.get_agv(serial)
            instance_store.ensure_instance_files(
                serial,
                manufacturer=str(getattr(info, "manufacturer", "")) if info else None,
                vda_version=str(getattr(info, "vda_version", "")) if info else None,
            )
            if not _has_instance_binary(serial):
                try:
                    instance_store.sync_binary(serial, overwrite=True)
                except Exception:
                    pass
            need_build = not _has_instance_binary(serial)
            yield sse_pack("status", {"serial": serial, "phase": "precheck", "need_build": bool(need_build)})
            if need_build:
                yield sse_pack("status", {"serial": serial, "phase": "build_start"})
                build_script = project_root / "SimAGV" / "build.sh"
                if not build_script.exists():
                    raise RuntimeError("build script missing")
                ok_line_path: str = ""
                with _build_lock:
                    proc = subprocess.Popen(
                        ["bash", str(build_script), "--name", "SimAGV"],
                        cwd=str(project_root),
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1,
                    )
                    out = proc.stdout
                    if out is not None:
                        for line in out:
                            text_line = str(line).rstrip("\n")
                            if text_line.startswith("OK "):
                                ok_line_path = text_line[3:].strip()
                            if text_line:
                                yield sse_pack("log", {"serial": serial, "line": text_line})
                    rc = proc.wait()
                    if rc != 0:
                        raise RuntimeError(f"build failed: exit={rc}")
                built_bin: Path | None = None
                if ok_line_path:
                    try:
                        cand = Path(ok_line_path)
                        if cand.exists() and cand.is_file():
                            built_bin = cand
                    except Exception:
                        built_bin = None
                if built_bin is None:
                    cand2 = project_root / "SimAGV" / "build" / "SimAGV"
                    if cand2.exists() and cand2.is_file():
                        built_bin = cand2
                if built_bin is None:
                    legacy_bin = project_root / "SimAGV" / "build" / "AMB-01"
                    if legacy_bin.exists() and legacy_bin.is_file():
                        built_bin = legacy_bin
                if built_bin is None:
                    raise RuntimeError("build output missing")
                instance_store.sync_binary_from(serial, built_bin, overwrite=True)
                yield sse_pack("status", {"serial": serial, "phase": "build_done"})
            else:
                instance_store.ensure_instance_files(serial)
            _write_ecosystem_files([a.serial_number for a in agv_manager.list_agvs()])
            yield sse_pack("status", {"serial": serial, "phase": "pm2_start"})
            rc, out, err = _pm2_run(["start", str(project_root / "ecosystem.config.js"), "--only", safe])
            if rc != 0:
                raise RuntimeError(err or out or "pm2 start failed")
            yield sse_pack("done", {"serial": serial, "started": True, "built": bool(need_build)})
        except Exception as e:
            yield sse_pack("fail", {"serial": serial, "started": False, "error": str(e)})

    return StreamingResponse(
        stream_iter(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )

@app.post("/api/agvs/{serial}/pm2/stop")
def pm2_stop_agv(serial: str):
    try:
        import time
        safe = _safe_name(serial)
        try:
            info = agv_manager.get_agv(serial)
            instance_store.ensure_instance_files(
                serial,
                manufacturer=str(getattr(info, "manufacturer", "")) if info else None,
                vda_version=str(getattr(info, "vda_version", "")) if info else None,
            )
            from .state_store import global_store
            rt = global_store.get_runtime(serial)
            pos = getattr(rt, "position", None)
            if pos is not None:
                instance_store.update_initial_pose(serial, getattr(pos, "x", None), getattr(pos, "y", None), getattr(pos, "theta", None))
            map_id = None
            try:
                st = agv_manager.get_status(serial)
                map_id = getattr(st, "current_map", None) if st else None
            except Exception:
                map_id = None
            if not map_id:
                map_id = getattr(rt, "current_map", None)
            if map_id:
                instance_store.persist_runtime_snapshot(serial, _canonicalize_map_id(map_id))
            overrides = getattr(app.state, "sim_settings_by_agv", {})
            patch = overrides.get(serial, {}) if isinstance(overrides, dict) else {}
            patch_for_file = {k: v for k, v in (patch or {}).items() if k != "frontend_poll_interval_ms" and v is not None}
            if map_id and "map_id" not in patch_for_file:
                patch_for_file["map_id"] = _canonicalize_map_id(map_id)
            if patch_for_file:
                instance_store.persist_sim_settings(serial, patch_for_file)
        except Exception:
            pass
        apps = _pm2_jlist()
        candidates = []
        for nm in (safe, f"agv-{safe}"):
            if nm not in candidates:
                candidates.append(nm)
        for proc in apps:
            try:
                name = str(proc.get("name") or "")
                if not name:
                    continue
                if name == safe or name == f"agv-{safe}":
                    continue
                env = proc.get("pm2_env") or {}
                ns = str(env.get("namespace") or "")
                if ns != "simagv":
                    continue
                if name == str(serial) or name == safe:
                    if name not in candidates:
                        candidates.insert(0, name)
            except Exception:
                continue
        last_out = ""
        last_err = ""
        for name in candidates:
            rc, out, err = _pm2_run(["stop", name])
            last_out = out or last_out
            last_err = err or last_err
            if rc == 0:
                time.sleep(0.2)
                apps_after = _pm2_jlist()
                for proc in apps_after:
                    try:
                        if str(proc.get("name") or "") != name:
                            continue
                        env = proc.get("pm2_env") or {}
                        st = str(env.get("status") or "").lower()
                        if st and st not in {"stopped", "stopping"}:
                            _pm2_run(["delete", name])
                            time.sleep(0.2)
                        break
                    except Exception:
                        break
                mem_total_bytes = _get_mem_total_bytes()
                agv_ps, _equip_ps = _ps_list_processes(mem_total_bytes)
                still_running_after = any(str(p.get("name") or "") == safe or str(p.get("name") or "") == str(serial) for p in agv_ps)
                if still_running_after:
                    raise HTTPException(status_code=500, detail="pm2 stop succeeded but process still running")
                return {"stopped": True, "serial": serial, "name": name}
            merged = f"{out}\n{err}".strip().lower()
            if "not found" in merged or "unknown process" in merged or "process or namespace" in merged:
                continue
        mem_total_bytes = _get_mem_total_bytes()
        agv_ps, _equip_ps = _ps_list_processes(mem_total_bytes)
        still_running = any(str(p.get("name") or "") == safe or str(p.get("name") or "") == str(serial) for p in agv_ps)
        if still_running:
            raise HTTPException(status_code=409, detail="process running but not found in pm2; check PM2_HOME/user")
        return {"stopped": True, "serial": serial, "note": "already stopped"}
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/agvs/{serial}/pose/persist")
def persist_agv_pose(serial: str):
    try:
        instance_store.ensure_instance_files(serial)
        from .state_store import global_store
        rt = global_store.get_runtime(serial)
        pos = getattr(rt, "position", None)
        if pos is None:
            return {"saved": False, "serial": serial, "reason": "missing_position"}
        x = getattr(pos, "x", None)
        y = getattr(pos, "y", None)
        theta = getattr(pos, "theta", None)
        if x is None or y is None:
            return {"saved": False, "serial": serial, "reason": "missing_xy"}
        instance_store.update_initial_pose(serial, x, y, theta)
        return {"saved": True, "serial": serial, "pose_x": float(x), "pose_y": float(y), "pose_theta": float(theta or 0.0)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# 全局仿真设置（热加载）：对所有已注册 AGV 生效
@app.post("/api/sim/settings")
def post_sim_settings_global(body: SimSettingsPatch):
    publisher = getattr(app.state, "mqtt_publisher", None)
    # 参数范围校验（与按实例接口一致）
    errors: list[str] = []
    def add_err(msg: str):
        errors.append(msg)
    if body.speed is not None and not (0.0 <= float(body.speed) <= 2.0):
        add_err("速度(speed)必须在[0,2]范围内")
    if body.sim_time_scale is not None:
        v = float(body.sim_time_scale)
        if not (v > 0.0 and v <= 10.0):
            add_err("时间缩放(sim_time_scale)必须在(0,10]范围内")
    if body.state_frequency is not None:
        v = int(body.state_frequency)
        if not (v >= 1 and v <= 10):
            add_err("状态频率(state_frequency)必须为[1,10]的正整数")
    if body.visualization_frequency is not None:
        v = int(body.visualization_frequency)
        if not (v >= 1 and v <= 10):
            add_err("可视化频率(visualization_frequency)必须为[1,10]的正整数")
    if body.action_time is not None:
        v = float(body.action_time)
        if not (v >= 1.0 and v <= 10.0):
            add_err("动作时长(action_time)必须在[1,10]范围内")
    if body.frontend_poll_interval_ms is not None:
        v = int(body.frontend_poll_interval_ms)
        if not (v >= 10 and v <= 1000):
            add_err("前端轮询(frontend_poll_interval_ms)必须为[10,1000]的正整数")
    if body.battery_default is not None:
        v = float(body.battery_default)
        if not (v > 0.0 and v <= 100.0):
            add_err("默认电量(battery_default)必须在(0,100]范围内")
    if body.battery_idle_drain_per_min is not None:
        v = float(body.battery_idle_drain_per_min)
        if not (v >= 1.0 and v <= 100.0):
            add_err("空闲耗电(battery_idle_drain_per_min)必须在[1,100]范围内")
    if body.battery_move_empty_multiplier is not None:
        v = float(body.battery_move_empty_multiplier)
        if not (v >= 1.0 and v <= 100.0):
            add_err("空载耗电系数(battery_move_empty_multiplier)必须在[1,100]范围内")
    if body.battery_move_loaded_multiplier is not None:
        v = float(body.battery_move_loaded_multiplier)
        if not (v >= 1.0 and v <= 100.0):
            add_err("载重耗电系数(battery_move_loaded_multiplier)必须在[1,100]范围内")
    if body.battery_charge_per_min is not None:
        v = float(body.battery_charge_per_min)
        if not (v >= 1.0 and v <= 100.0):
            add_err("充电速度(battery_charge_per_min)必须在[1,100]范围内")
    if errors:
        raise HTTPException(status_code=400, detail="; ".join(errors))
    try:
        # 雷达/安全参数范围校验
        if body.radar_fov_deg is not None:
            v = float(body.radar_fov_deg)
            if not (1.0 <= v <= 360.0):
                add_err("雷达FOV(radar_fov_deg)必须在[1,360]范围内")
        if body.radar_radius_m is not None:
            v = float(body.radar_radius_m)
            if not (0.01 <= v <= 10.0):
                add_err("雷达半径(radar_radius_m)必须在[0.01,10]范围内")
        if body.safety_scale is not None:
            v = float(body.safety_scale)
            if not (1.0 <= v <= 5.0):
                add_err("安全范围系数(safety_scale)必须在[1.0,5.0]范围内")
        if errors:
            raise HTTPException(status_code=400, detail="; ".join(errors))
        # 更新运行态的前端轮询覆盖值（若提交了该项）
        if body.frontend_poll_interval_ms is not None:
            try:
                app.state.frontend_poll_interval_ms = int(body.frontend_poll_interval_ms)
            except Exception:
                pass
        # 记录最近一次的全局设置覆盖值（用于前端预填）
        try:
            update = {k: v for k, v in body.model_dump().items() if v is not None}
            app.state.sim_settings_global = update
        except Exception:
            pass
        # 对所有已注册 AGV 发布 simConfig
        infos = agv_manager.list_agvs()
        for info in infos:
            if publisher:
                try:
                    publisher.publish_sim_settings(info, body)
                except Exception as e:
                    print(f"[SimConfig] publish failed for {info.serial_number}: {e}")
        # 同步运行态覆盖映射，保证按实例查询也能看到最新值
        try:
            current = getattr(app.state, "sim_settings_by_agv", None)
            if current is None:
                app.state.sim_settings_by_agv = {}
                current = app.state.sim_settings_by_agv
            for info in infos:
                prev = current.get(info.serial_number, {})
                prev.update({k: v for k, v in body.model_dump().items() if v is not None})
                current[info.serial_number] = prev
        except Exception:
            pass
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish global sim settings failed: {e}")
    return {"updated": True, "scope": "global", "count": len(infos)}

# 获取全局仿真设置：合并默认配置与最近一次提交的全局覆盖值
@app.get("/api/sim/settings")
def get_sim_settings_global():
    ov = getattr(app.state, "sim_settings_global", {})
    def pick(name: str, default):
        if isinstance(ov, dict) and name in ov and ov.get(name) is not None:
            return ov.get(name)
        return default
    fp_runtime = getattr(app.state, "frontend_poll_interval_ms", 1000)
    result = {
        "action_time": pick("action_time", 1.0),
        "speed": pick("speed", 1.0),
        # 物理参数（用于前端物理参数弹窗预填与仿真约束）
        "speed_min": pick("speed_min", 0.01),
        "speed_max": pick("speed_max", 2.0),
        "acceleration_max": pick("acceleration_max", 2.0),
        "deceleration_max": pick("deceleration_max", 2.0),
        "height_min": pick("height_min", 0.01),
        "height_max": pick("height_max", 0.10),
        "width": pick("width", 0.745),
        "length": pick("length", 1.03),
        "state_frequency": pick("state_frequency", 10),
        "visualization_frequency": pick("visualization_frequency", 1),
        "map_id": pick("map_id", "default"),
        "sim_time_scale": pick("sim_time_scale", 1.0),
        "battery_default": pick("battery_default", 100.0),
        "battery_idle_drain_per_min": pick("battery_idle_drain_per_min", 1.0),
        "battery_move_empty_multiplier": pick("battery_move_empty_multiplier", 1.5),
        "battery_move_loaded_multiplier": pick("battery_move_loaded_multiplier", 2.5),
        "battery_charge_per_min": pick("battery_charge_per_min", 10.0),
        "frontend_poll_interval_ms": int(fp_runtime),
    }
    return result

@app.post("/api/agv/{serial_number}/move/rotate", response_model=StatusResponse)
async def move_rotate(serial_number: str, body: RotateRequest):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    dtheta = float(body.dtheta)
    angle = abs(dtheta)
    if angle <= 1e-6:
        status = agv_manager.get_status(serial_number)
        if not status:
            raise HTTPException(status_code=404, detail="AGV status not found")
        return status
    publisher = getattr(app.state, "mqtt_publisher", None)
    if publisher:
        desired_dt = 0.05
        vw = max(0.2, min(2.0, angle / max(1e-3, desired_dt)))
        if dtheta < 0:
            vw = -vw
        try:
            publisher.publish_turn(info, angle_rad=angle, vw_radps=vw)
        except Exception as e:
            print(f"Publish turn failed: {e}")
        status = agv_manager.get_status(serial_number)
        if not status:
            raise HTTPException(status_code=404, detail="AGV status not found")
        return status
    rt = await execute_rotation_movement(serial_number, dtheta, agv_manager, ws_manager)
    if not rt:
        raise HTTPException(status_code=404, detail="AGV not found")
    status = agv_manager.get_status(serial_number)
    return status

def _convert_order_camel_to_snake(d: dict) -> dict:
    """Convert a VDA5050 order payload from camelCase to snake_case for dataclass validation.

    Keeps unknown keys as-is; recursively converts nodes, edges, actions, trajectory, and nodePosition.
    """
    def map_keys(obj):
        if isinstance(obj, list):
            return [map_keys(x) for x in obj]
        if not isinstance(obj, dict):
            return obj
        m = {}
        # top-level/order-level mappings
        key_map = {
            "headerId": "header_id",
            "serialNumber": "serial_number",
            "orderId": "order_id",
            "orderUpdateId": "order_update_id",
            "zoneSetId": "zone_set_id",
        }
        # node-level
        node_map = {
            "nodeId": "node_id",
            "sequenceId": "sequence_id",
            "nodeDescription": "node_description",
            "nodePosition": "node_position",
        }
        # nodePosition-level
        np_map = {
            "mapId": "map_id",
            "mapDescription": "map_description",
            "allowedDeviationXY": "allowed_deviation_xy",
            "allowedDeviationTheta": "allowed_deviation_theta",
        }
        # edge-level
        edge_map = {
            "edgeId": "edge_id",
            "sequenceId": "sequence_id",
            "startNodeId": "start_node_id",
            "endNodeId": "end_node_id",
            "edgeDescription": "edge_description",
            "maxSpeed": "max_speed",
            "maxHeight": "max_height",
            "minHeight": "min_height",
            "orientationType": "orientation_type",
            "rotationAllowed": "rotation_allowed",
            "maxRotationSpeed": "max_rotation_speed",
            "length": "length",
            "direction": "direction",
            "orientation": "orientation",
            "trajectory": "trajectory",
        }
        # trajectory-level
        traj_map = {
            "knotVector": "knot_vector",
            "controlPoints": "control_points",
        }
        # action-level
        act_map = {
            "actionType": "action_type",
            "actionId": "action_id",
            "blockingType": "blocking_type",
            "actionDescription": "action_description",
            "actionParameters": "action_parameters",
        }
        # decide which map to use per object
        # heuristics: look for sentinel keys
        if "nodes" in obj or "edges" in obj:
            # order-level
            for k, v in obj.items():
                nk = key_map.get(k, k)
                if nk in ("nodes", "edges"):
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        # detect node
        if any(k in obj for k in ("nodeId", "node_id")):
            for k, v in obj.items():
                nk = node_map.get(k, k)
                if nk == "node_position":
                    m[nk] = map_keys(v)
                elif nk == "actions":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        # detect nodePosition
        if any(k in obj for k in ("mapId", "map_id")):
            for k, v in obj.items():
                nk = np_map.get(k, k)
                m[nk] = map_keys(v)
            return m
        # detect edge
        if any(k in obj for k in ("edgeId", "edge_id", "startNodeId", "start_node_id")):
            for k, v in obj.items():
                nk = edge_map.get(k, k)
                if nk == "trajectory":
                    m[nk] = map_keys(v)
                elif nk == "actions":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        # detect trajectory
        if any(k in obj for k in ("knotVector", "knot_vector", "controlPoints", "control_points")):
            for k, v in obj.items():
                nk = traj_map.get(k, k)
                m[nk] = map_keys(v)
            return m
        # detect action
        if any(k in obj for k in ("actionType", "action_type", "actionId", "action_id")):
            for k, v in obj.items():
                nk = act_map.get(k, k)
                if nk == "action_parameters":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        # default: passthrough keys
        for k, v in obj.items():
            m[k] = map_keys(v)
        return m

    return map_keys(d)

def _convert_order_snake_to_camel(d: dict) -> dict:
    """Convert a VDA5050 order payload from snake_case to camelCase for publishing.

    Recursively converts known keys across order, nodes, edges, actions, trajectory, and nodePosition.
    Unknown keys are preserved as-is.
    """
    def map_keys(obj):
        if isinstance(obj, list):
            return [map_keys(x) for x in obj]
        if not isinstance(obj, dict):
            return obj
        m = {}
        # top-level/order-level mappings
        key_map = {
            "header_id": "headerId",
            "serial_number": "serialNumber",
            "order_id": "orderId",
            "order_update_id": "orderUpdateId",
            "zone_set_id": "zoneSetId",
        }
        # node-level
        node_map = {
            "node_id": "nodeId",
            "sequence_id": "sequenceId",
            "node_description": "nodeDescription",
            "node_position": "nodePosition",
        }
        # nodePosition-level
        np_map = {
            "map_id": "mapId",
            "map_description": "mapDescription",
            "allowed_deviation_xy": "allowedDeviationXY",
            "allowed_deviation_theta": "allowedDeviationTheta",
        }
        # edge-level
        edge_map = {
            "edge_id": "edgeId",
            "sequence_id": "sequenceId",
            "start_node_id": "startNodeId",
            "end_node_id": "endNodeId",
            "edge_description": "edgeDescription",
            "max_speed": "maxSpeed",
            "max_height": "maxHeight",
            "min_height": "minHeight",
            "orientation_type": "orientationType",
            "rotation_allowed": "rotationAllowed",
            "max_rotation_speed": "maxRotationSpeed",
            "length": "length",
            "direction": "direction",
            "orientation": "orientation",
            "trajectory": "trajectory",
        }
        # trajectory-level
        traj_map = {
            "knot_vector": "knotVector",
            "control_points": "controlPoints",
        }
        # action-level
        act_map = {
            "action_type": "actionType",
            "action_id": "actionId",
            "blocking_type": "blockingType",
            "action_description": "actionDescription",
            "action_parameters": "actionParameters",
        }
        # decide which map to use per object
        if "nodes" in obj or "edges" in obj:
            for k, v in obj.items():
                nk = key_map.get(k, k)
                if nk in ("nodes", "edges"):
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        if any(k in obj for k in ("node_id", "nodeId")):
            for k, v in obj.items():
                nk = node_map.get(k, k)
                if nk == "nodePosition":
                    m[nk] = map_keys(v)
                elif nk == "actions":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        if any(k in obj for k in ("map_id", "mapId")):
            for k, v in obj.items():
                nk = np_map.get(k, k)
                m[nk] = map_keys(v)
            return m
        if any(k in obj for k in ("edge_id", "edgeId", "start_node_id", "startNodeId")):
            for k, v in obj.items():
                nk = edge_map.get(k, k)
                if nk == "trajectory":
                    m[nk] = map_keys(v)
                elif nk == "actions":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        if any(k in obj for k in ("knot_vector", "knotVector", "control_points", "controlPoints")):
            for k, v in obj.items():
                nk = traj_map.get(k, k)
                m[nk] = map_keys(v)
            return m
        if any(k in obj for k in ("action_type", "actionType", "action_id", "actionId")):
            for k, v in obj.items():
                nk = act_map.get(k, k)
                if nk == "actionParameters":
                    m[nk] = map_keys(v)
                else:
                    m[nk] = map_keys(v)
            return m
        for k, v in obj.items():
            m[k] = map_keys(v)
        return m

    return map_keys(d)

# 发布 VDA5050 订单到 MQTT
@app.post("/api/agv/{serial_number}/order")
def publish_order(serial_number: str, body: dict = Body(...)):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    # 简单结构校验（兼容 camelCase：转换为 snake_case 进行 dataclass 构造）
    try:
        snake_body = _convert_order_camel_to_snake(body)
        # 确保节点/边包含 actions 字段；若缺失则补空数组，避免设备端解析后缺失
        try:
            if isinstance(snake_body.get("nodes"), list):
                for n in snake_body["nodes"]:
                    if isinstance(n, dict) and "actions" not in n:
                        n["actions"] = []
            if isinstance(snake_body.get("edges"), list):
                for e in snake_body["edges"]:
                    if isinstance(e, dict) and "actions" not in e:
                        e["actions"] = []
        except Exception:
            pass
        # dataclass 构造需要 zone_set_id 键，但不希望在发布到 MQTT 时出现 null
        original_has_zone_set = ("zoneSetId" in body) or ("zone_set_id" in body)
        if "zone_set_id" not in snake_body:
            snake_body["zone_set_id"] = None
        if VDAOrder is not None:
            VDAOrder(**snake_body)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Invalid order payload: {e}")
    sn_in_body = str(body.get("serialNumber", body.get("serial_number", "")).strip())
    if sn_in_body and sn_in_body != info.serial_number:
        raise HTTPException(status_code=400, detail="serial_number mismatch")
    publisher = getattr(app.state, "mqtt_publisher", None)
    if not publisher:
        raise HTTPException(status_code=500, detail="MQTT publisher not available")
    try:
        # 统一使用 camelCase 进行发布，保证设备端解析一致
        # 使用经过补全的 snake_body 进行转换，以确保 actions 存在
        camel_body = _convert_order_snake_to_camel(snake_body)
        # 若原始请求未提供 zoneSetId，且其值为 null，则移除该键，避免发布 "zoneSetId": null
        if not original_has_zone_set and camel_body.get("zoneSetId") is None:
            try:
                del camel_body["zoneSetId"]
            except Exception:
                pass
        publisher.publish_order(info, camel_body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish order failed: {e}")
    # 返回兼容的 orderId（同时提供两种命名以兼容旧前端）
    _oid = body.get("orderId") or body.get("order_id")
    return {"published": True, "serial_number": serial_number, "orderId": _oid, "order_id": _oid}

@app.post("/api/agv/{serial_number}/instant")
def publish_agv_instant_actions(serial_number: str, body: dict = Body(...)):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    sn_in_body = str(body.get("serialNumber", body.get("serial_number", "")).strip())
    if sn_in_body and sn_in_body != info.serial_number:
        raise HTTPException(status_code=400, detail="serial_number mismatch")
    publisher = getattr(app.state, "mqtt_publisher", None)
    if not publisher:
        raise HTTPException(status_code=500, detail="MQTT publisher not available")
    try:
        publisher.publish_instant_actions(info, body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish instantActions failed: {e}")
    return {"published": True, "serial_number": serial_number}

@app.post("/api/agv/{serial_number}/instant/switch-map")
def publish_switch_map(serial_number: str, body: SwitchMapRequest):
    info = agv_manager.get_agv(serial_number)
    if not info:
        raise HTTPException(status_code=404, detail="AGV not found")
    publisher = getattr(app.state, "mqtt_publisher", None)
    if not publisher:
        raise HTTPException(status_code=500, detail="MQTT publisher not available")
    try:
        publisher.publish_switch_map(
            info,
            map_name=body.map,
            switch_point=body.switch_point,
            center_x=body.center_x,
            center_y=body.center_y,
            initiate_angle=body.initiate_angle,
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Publish switchMap failed: {e}")

    try:
        overrides = getattr(app.state, "sim_settings_by_agv", {})
        if not isinstance(overrides, dict):
            overrides = {}
        prev = overrides.get(serial_number, {})
        if not isinstance(prev, dict):
            prev = {}
        prev["map_id"] = _canonicalize_map_id(body.map)
        overrides[serial_number] = prev
        app.state.sim_settings_by_agv = overrides
    except Exception:
        pass
    try:
        from .schemas import DynamicConfigUpdate, Position
        pos = None
        if body.center_x is not None and body.center_y is not None:
            pos = Position(
                x=float(body.center_x),
                y=float(body.center_y),
                theta=float(body.initiate_angle) if body.initiate_angle is not None else None,
            )
        agv_manager.update_dynamic(serial_number, DynamicConfigUpdate(current_map=_canonicalize_map_id(body.map), position=pos))
    except Exception:
        pass

    return {"published": True, "serial_number": serial_number}

# 手动触发运行态快照持久化（用于优雅停止前确保坐标保存）
@app.post("/api/system/persist-runtime")
def persist_runtime_snapshots():
    saved = 0
    try:
        overrides = getattr(app.state, "sim_settings_by_agv", {})
    except Exception:
        overrides = {}
    for info in agv_manager.list_agvs():
        try:
            # 尝试使用后端集中管理的状态（来自 MQTTBridge）
            status = agv_manager.get_status(info.serial_number)
            if status:
                try:
                    instance_store.ensure_instance_files(info.serial_number, manufacturer=info.manufacturer, vda_version=info.vda_version)
                except Exception:
                    pass
                try:
                    instance_store.persist_runtime_snapshot(info.serial_number, status.current_map)
                except Exception:
                    pass
                try:
                    if status.position:
                        instance_store.update_initial_pose(info.serial_number, status.position.x, status.position.y, status.position.theta)
                except Exception:
                    pass
                saved += 1
        except Exception:
            pass
        # 同步最近一次仿真设置覆盖（可选）
        try:
            patch = overrides.get(info.serial_number, {}) if isinstance(overrides, dict) else {}
            if patch:
                patch_for_file = {k: v for k, v in (patch or {}).items() if k != "frontend_poll_interval_ms" and v is not None}
                if patch_for_file:
                    instance_store.persist_sim_settings(info.serial_number, patch_for_file)
        except Exception:
            pass
    return {"saved": saved}
