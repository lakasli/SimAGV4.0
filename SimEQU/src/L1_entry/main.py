from __future__ import annotations
import sys
from pathlib import Path

simagv_root = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(simagv_root))

from SimEQU.devices.elevator.device import run


def main() -> None:
    dir_path = simagv_root / "devices" / "elevator"
    run(str(dir_path))


if __name__ == "__main__":
    main()
