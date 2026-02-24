from __future__ import annotations

import json
import os
import shutil
import threading
from pathlib import Path
from typing import Any, Dict, Optional


_PHYSICAL_SNAKE_TO_FACTSHEET_KEY: dict[str, str] = {
    "speed_min": "speedMin",
    "speed_max": "speedMax",
    "acceleration_max": "accelerationMax",
    "deceleration_max": "decelerationMax",
    "height_min": "heightMin",
    "height_max": "heightMax",
    "width": "width",
    "length": "length",
}


def _sanitize_name(name: str) -> str:
    s = str(name or "").strip()
    if not s:
        return "unknown"
    out: list[str] = []
    for c in s:
        if c.isalnum() or c in ("-", "_"):
            out.append(c)
        else:
            out.append("-")
    return "".join(out)


def _parse_scalar(raw: str) -> Any:
    s = str(raw).strip()
    if not s:
        return ""
    if s in {"null", "~"}:
        return None
    low = s.lower()
    if low == "true":
        return True
    if low == "false":
        return False
    if (s.startswith('"') and s.endswith('"')) or (s.startswith("'") and s.endswith("'")):
        q = s[0]
        inner = s[1:-1]
        if q == '"':
            try:
                return json.loads(s)
            except Exception:
                return inner
        return inner
    try:
        if "." in s or "e" in low:
            return float(s)
        return int(s)
    except Exception:
        return s


def _parse_simple_yaml(text: str) -> Dict[str, Any]:
    root: Dict[str, Any] = {}
    stack: list[tuple[int, Dict[str, Any]]] = [(-1, root)]
    for raw_line in (text or "").splitlines():
        if not raw_line.strip():
            continue
        if raw_line.lstrip().startswith("#"):
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        line = raw_line.strip()
        if ":" not in line:
            continue
        key, rest = line.split(":", 1)
        key = key.strip()
        value_str = rest.strip()
        while stack and indent <= stack[-1][0]:
            stack.pop()
        cur = stack[-1][1] if stack else root
        if value_str == "":
            nxt: Dict[str, Any] = {}
            cur[key] = nxt
            stack.append((indent, nxt))
            continue
        cur[key] = _parse_scalar(value_str)
    return root


def _dump_scalar(val: Any) -> str:
    if val is None:
        return "null"
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, (int, float)):
        return str(val)
    return json.dumps(str(val), ensure_ascii=False)


def _dump_simple_yaml(data: Dict[str, Any], indent: int = 0) -> str:
    lines: list[str] = []
    for k, v in (data or {}).items():
        key = str(k)
        if isinstance(v, dict):
            lines.append(" " * indent + f"{key}:")
            lines.append(_dump_simple_yaml(v, indent + 2).rstrip("\n"))
        else:
            lines.append(" " * indent + f"{key}: {_dump_scalar(v)}")
    return "\n".join(lines).rstrip("\n") + "\n"


def _get_path(d: Dict[str, Any], path: list[str]) -> Any:
    cur: Any = d
    for p in path:
        if not isinstance(cur, dict):
            return None
        cur = cur.get(p)
    return cur


def _ensure_path(d: Dict[str, Any], path: list[str]) -> Dict[str, Any]:
    cur: Dict[str, Any] = d
    for p in path:
        nxt = cur.get(p)
        if not isinstance(nxt, dict):
            nxt = {}
            cur[p] = nxt
        cur = nxt
    return cur


def _atomic_write_text(fp: Path, text: str) -> None:
    fp.parent.mkdir(parents=True, exist_ok=True)
    tmp = fp.with_suffix(fp.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, fp)


class InstanceFileStore:
    def __init__(
        self,
        instances_root: Path,
        template_config_yaml: Path,
        template_binary: Path,
    ) -> None:
        self.instances_root = Path(instances_root)
        self.template_config_yaml = Path(template_config_yaml)
        self.template_binary = Path(template_binary)
        self.instances_root.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()

    def instance_dir(self, serial_number: str) -> Path:
        return self.instances_root / _sanitize_name(serial_number)

    def config_yaml_path(self, serial_number: str) -> Path:
        return self.instance_dir(serial_number) / "config.yaml"

    def binary_exec_name(self, serial_number: str) -> str:
        return _sanitize_name(serial_number)

    def binary_path(self, serial_number: str) -> Path:
        return self.instance_dir(serial_number) / self.binary_exec_name(serial_number)

    def sync_binary(self, serial_number: str, overwrite: bool = True) -> Path:
        with self._lock:
            inst_dir = self.instance_dir(serial_number)
            inst_dir.mkdir(parents=True, exist_ok=True)
            bin_dst = self.binary_path(serial_number)
            src_bin = self.template_binary
            if not src_bin.exists():
                legacy_src = src_bin.parent / "AMB-01"
                if legacy_src.exists():
                    src_bin = legacy_src
            if not src_bin.exists():
                return bin_dst
            if not bin_dst.exists():
                shutil.copy2(src_bin, bin_dst)
                try:
                    bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
                except Exception:
                    pass
                return bin_dst
            if overwrite:
                shutil.copy2(src_bin, bin_dst)
                try:
                    bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
                except Exception:
                    pass
                return bin_dst
            try:
                if src_bin.stat().st_mtime > bin_dst.stat().st_mtime:
                    shutil.copy2(src_bin, bin_dst)
                    try:
                        bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
                    except Exception:
                        pass
            except Exception:
                pass
            return bin_dst

    def sync_binary_from(self, serial_number: str, src_binary: Path, overwrite: bool = True) -> Path:
        with self._lock:
            inst_dir = self.instance_dir(serial_number)
            inst_dir.mkdir(parents=True, exist_ok=True)
            bin_dst = self.binary_path(serial_number)
            src_binary = Path(src_binary)
            if not src_binary.exists():
                return bin_dst
            if bin_dst.exists() and not overwrite:
                return bin_dst
            shutil.copy2(src_binary, bin_dst)
            try:
                bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
            except Exception:
                pass
            return bin_dst

    def ensure_instance_files(self, serial_number: str, manufacturer: str | None = None, vda_version: str | None = None) -> Path:
        with self._lock:
            inst_dir = self.instance_dir(serial_number)
            inst_dir.mkdir(parents=True, exist_ok=True)

            bin_dst = self.binary_path(serial_number)
            legacy_bin = inst_dir / "AMB-01"
            if not bin_dst.exists():
                if legacy_bin.exists():
                    try:
                        os.replace(legacy_bin, bin_dst)
                    except Exception:
                        shutil.copy2(legacy_bin, bin_dst)
                        try:
                            legacy_bin.unlink(missing_ok=True)
                        except Exception:
                            pass
                    try:
                        bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
                    except Exception:
                        pass
                elif self.template_binary.exists():
                    shutil.copy2(self.template_binary, bin_dst)
                    try:
                        bin_dst.chmod(bin_dst.stat().st_mode | 0o111)
                    except Exception:
                        pass
            else:
                try:
                    if legacy_bin.exists() and legacy_bin.is_file() and legacy_bin.resolve() != bin_dst.resolve():
                        legacy_bin.unlink(missing_ok=True)
                except Exception:
                    pass

            cfg_dst = self.config_yaml_path(serial_number)
            if not cfg_dst.exists():
                legacy_cfg = inst_dir / "SimAGV" / "config.yaml"
                if legacy_cfg.exists():
                    shutil.copy2(legacy_cfg, cfg_dst)
                elif self.template_config_yaml.exists():
                    shutil.copy2(self.template_config_yaml, cfg_dst)

            try:
                self.update_vehicle_identity(serial_number, manufacturer=manufacturer, vda_version=vda_version)
            except Exception:
                pass
            try:
                self.migrate_sim_config_from_hot_to_config(serial_number)
            except Exception:
                pass
            try:
                self.normalize_instance_config(serial_number)
            except Exception:
                pass

            return inst_dir

    def delete_instance_dir(self, serial_number: str) -> None:
        with self._lock:
            inst_dir = self.instance_dir(serial_number)
            if inst_dir.exists() and inst_dir.is_dir():
                shutil.rmtree(inst_dir, ignore_errors=True)

    def load_config_yaml(self, serial_number: str) -> Dict[str, Any]:
        fp = self.config_yaml_path(serial_number)
        with self._lock:
            if not fp.exists():
                legacy_fp = self.instance_dir(serial_number) / "SimAGV" / "config.yaml"
                if legacy_fp.exists():
                    return _parse_simple_yaml(legacy_fp.read_text(encoding="utf-8"))
                return {}
            return _parse_simple_yaml(fp.read_text(encoding="utf-8"))

    def save_config_yaml(self, serial_number: str, data: Dict[str, Any]) -> None:
        fp = self.config_yaml_path(serial_number)
        with self._lock:
            _atomic_write_text(fp, _dump_simple_yaml(data))

    def update_vehicle_identity(self, serial_number: str, manufacturer: str | None = None, vda_version: str | None = None) -> None:
        cfg = self.load_config_yaml(serial_number)
        vehicle = _ensure_path(cfg, ["vehicle"])
        vehicle["serial_number"] = str(serial_number)
        if manufacturer is not None:
            vehicle["manufacturer"] = str(manufacturer)
        if vda_version is not None:
            vehicle["vda_version"] = str(vda_version)
        self.save_config_yaml(serial_number, cfg)

    def read_sim_settings(self, serial_number: str) -> Dict[str, Any]:
        cfg = self.load_config_yaml(serial_number)
        sim_cfg = _get_path(cfg, ["sim_config"])
        if not isinstance(sim_cfg, dict) or not sim_cfg:
            sim_cfg = self._read_legacy_hot_sim_config(serial_number)
        out: Dict[str, Any] = {}
        keys = [
            "action_time",
            "speed",
            "state_frequency",
            "visualization_frequency",
            "map_id",
            "sim_time_scale",
            "battery_default",
            "battery_idle_drain_per_min",
            "battery_move_empty_multiplier",
            "battery_move_loaded_multiplier",
            "battery_charge_per_min",
            "radar_fov_deg",
            "radar_radius_m",
            "safety_scale",
        ]
        for k in keys:
            if k in sim_cfg and sim_cfg.get(k) is not None:
                out[k] = sim_cfg.get(k)
        phys = _get_path(cfg, ["factsheet", "physicalParameters"])
        if isinstance(phys, dict):
            for snake_key, factsheet_key in _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.items():
                v = phys.get(factsheet_key)
                if v is not None:
                    out[snake_key] = v
        for snake_key in _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.keys():
            if snake_key not in out and snake_key in sim_cfg and sim_cfg.get(snake_key) is not None:
                out[snake_key] = sim_cfg.get(snake_key)
        return out

    def persist_sim_settings(self, serial_number: str, patch: Dict[str, Any]) -> None:
        cfg = self.load_config_yaml(serial_number)
        sim_cfg = _ensure_path(cfg, ["sim_config"])
        phys = _ensure_path(cfg, ["factsheet", "physicalParameters"])
        for k, v in (patch or {}).items():
            if v is None:
                continue
            factsheet_key = _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.get(k)
            if factsheet_key is not None:
                phys[factsheet_key] = v
            else:
                sim_cfg[k] = v
        for legacy_key in _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.keys():
            if legacy_key in sim_cfg:
                try:
                    del sim_cfg[legacy_key]
                except Exception:
                    pass
        self.save_config_yaml(serial_number, cfg)

    def persist_runtime_snapshot(self, serial_number: str, map_id: str | None) -> None:
        if map_id is None:
            return
        self.persist_sim_settings(serial_number, {"map_id": str(map_id)})

    def read_initial_pose(self, serial_number: str) -> Optional[Dict[str, float]]:
        cfg = self.load_config_yaml(serial_number)
        pose = _get_path(cfg, ["initial_pose"])
        if not isinstance(pose, dict):
            return None
        try:
            x = float(pose.get("pose_x"))
            y = float(pose.get("pose_y"))
            theta = float(pose.get("pose_theta"))
        except Exception:
            return None
        if not (isinstance(x, float) and isinstance(y, float) and isinstance(theta, float)):
            return None
        return {"pose_x": x, "pose_y": y, "pose_theta": theta}

    def update_initial_pose(self, serial_number: str, x: float | None, y: float | None, theta: float | None) -> None:
        if x is None and y is None and theta is None:
            return
        cfg = self.load_config_yaml(serial_number)
        pose = _ensure_path(cfg, ["initial_pose"])
        if x is not None:
            pose["pose_x"] = float(x)
        if y is not None:
            pose["pose_y"] = float(y)
        if theta is not None:
            pose["pose_theta"] = float(theta)
        self.save_config_yaml(serial_number, cfg)

    def _load_legacy_hot_config_yaml(self, serial_number: str) -> Dict[str, Any]:
        inst_dir = self.instance_dir(serial_number)
        fp = inst_dir / "hot_config.yaml"
        if not fp.exists():
            fp = inst_dir / "SimAGV" / "hot_config.yaml"
        if not fp.exists():
            return {}
        try:
            return _parse_simple_yaml(fp.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def _read_legacy_hot_sim_config(self, serial_number: str) -> Dict[str, Any]:
        hot = self._load_legacy_hot_config_yaml(serial_number)
        sim_cfg = _get_path(hot, ["sim_config"])
        if not isinstance(sim_cfg, dict):
            return {}
        return dict(sim_cfg)

    def migrate_sim_config_from_hot_to_config(self, serial_number: str) -> None:
        cfg = self.load_config_yaml(serial_number)
        sim_cfg = _get_path(cfg, ["sim_config"])
        has_sim_cfg = isinstance(sim_cfg, dict) and bool(sim_cfg)
        legacy_sim_cfg = self._read_legacy_hot_sim_config(serial_number)
        if has_sim_cfg or not legacy_sim_cfg:
            return
        dst = _ensure_path(cfg, ["sim_config"])
        for k, v in legacy_sim_cfg.items():
            if v is not None:
                dst[k] = v
        self.save_config_yaml(serial_number, cfg)

    def normalize_instance_config(self, serial_number: str) -> None:
        cfg = self.load_config_yaml(serial_number)
        if not isinstance(cfg, dict) or not cfg:
            return
        sim_cfg = _get_path(cfg, ["sim_config"])
        if not isinstance(sim_cfg, dict):
            sim_cfg = _ensure_path(cfg, ["sim_config"])
        phys = _get_path(cfg, ["factsheet", "physicalParameters"])
        if not isinstance(phys, dict):
            phys = _ensure_path(cfg, ["factsheet", "physicalParameters"])
        changed = False
        for snake_key, factsheet_key in _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.items():
            if factsheet_key not in phys and snake_key in sim_cfg and sim_cfg.get(snake_key) is not None:
                phys[factsheet_key] = sim_cfg.get(snake_key)
                changed = True
        for snake_key in _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.keys():
            if snake_key in sim_cfg:
                try:
                    del sim_cfg[snake_key]
                    changed = True
                except Exception:
                    pass
        inst_dir = self.instance_dir(serial_number)
        legacy_hot_paths = [inst_dir / "hot_config.yaml", inst_dir / "SimAGV" / "hot_config.yaml"]
        for fp in legacy_hot_paths:
            if not fp.exists():
                continue
            try:
                hot = _parse_simple_yaml(fp.read_text(encoding="utf-8"))
            except Exception:
                hot = {}
            hot_sim_cfg = _get_path(hot, ["sim_config"])
            if isinstance(hot_sim_cfg, dict):
                for k, v in hot_sim_cfg.items():
                    if v is None:
                        continue
                    factsheet_key = _PHYSICAL_SNAKE_TO_FACTSHEET_KEY.get(k)
                    if factsheet_key is not None:
                        if phys.get(factsheet_key) is None:
                            phys[factsheet_key] = v
                            changed = True
                    else:
                        if sim_cfg.get(k) is None:
                            sim_cfg[k] = v
                            changed = True
            try:
                fp.unlink(missing_ok=True)
            except Exception:
                pass
        if changed:
            self.save_config_yaml(serial_number, cfg)
