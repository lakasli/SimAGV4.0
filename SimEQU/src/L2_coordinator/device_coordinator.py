from __future__ import annotations
import time
from pathlib import Path
from typing import TYPE_CHECKING

from ..L3_molecules.base_device import EquipmentDevice
from ..L4_atoms.config_loader import load_config

if TYPE_CHECKING:
    from ..L4_atoms.config_loader import EquipmentConfig


class DeviceCoordinator:
    def __init__(self, device: EquipmentDevice) -> None:
        self.device = device

    def run(self, dir_path: str) -> None:
        cfg = load_config(Path(dir_path))
        self.device = self._create_device(cfg)
        self.start_loop()

    def _create_device(self, cfg: "EquipmentConfig") -> EquipmentDevice:
        return self.device

    def start_loop(self) -> None:
        self.device.start()
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            self.device.stop()
