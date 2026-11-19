"""Stage both StickS3 web installers and check that their writes leave NVS intact."""

import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NVS_START, NVS_END = 0x9000, 0xE000
SECTOR = 4096
ESP32_S3_CHIP = 9


def prepare(site, pio_build, boot_app0, pocket_merged, page_build):
    shutil.copytree(ROOT / "web", site, dirs_exist_ok=True)
    merged = pocket_merged.read_bytes()
    pio_boot_size = (pio_build / "bootloader.bin").stat().st_size

    installers = {
        "manifest.json": [
            ("pocket-nexus/bootloader.bin", 0x0, merged[:pio_boot_size]),
            ("pocket-nexus/partitions.bin", 0x8000, (pio_build / "partitions.bin").read_bytes()),
            ("pocket-nexus/boot-app0.bin", 0xE000, boot_app0.read_bytes()),
            ("pocket-nexus/firmware.bin", 0x10000, (pio_build / "firmware.bin").read_bytes()),
        ],
        "manifest-page-turner.json": [
            ("page-turner/bootloader.bin", 0x0, (page_build / "bootloader/bootloader.bin").read_bytes()),
            ("page-turner/partition-table.bin", 0x8000,
             (page_build / "partition_table/partition-table.bin").read_bytes()),
            ("page-turner/sticks3-page-turner.bin", 0x10000, (page_build / "sticks3-page-turner.bin").read_bytes()),
        ],
    }

    for manifest_name, parts in installers.items():
        manifest = json.loads((site / manifest_name).read_text(encoding="utf-8"))
        expected = [{"chipFamily": "ESP32-S3", "parts": [
            {"path": f"firmware/{name}", "offset": offset} for name, offset, _ in parts
        ]}]
        if manifest["builds"] != expected or manifest.get("new_install_prompt_erase") is not True:
            raise ValueError(f"{manifest_name} must use the expected parts and prompt before erasing")
        if manifest_name == "manifest-page-turner.json":
            project = json.loads((page_build / "project_description.json").read_text(encoding="utf-8"))
            if project["project_version"] != manifest["version"]:
                raise ValueError("Page turner version differs between firmware and manifest")

        previous_end = 0
        for name, offset, data in parts:
            end = offset + ((len(data) + SECTOR - 1) // SECTOR) * SECTOR
            if (not data or offset % SECTOR or offset < previous_end or end > 0x800000
                    or (offset < NVS_END and end > NVS_START)):
                raise ValueError(f"{name} has an invalid or overlapping flash range")
            if offset in (0, 0x10000):
                if data[0] != 0xE9 or int.from_bytes(data[12:14], "little") != ESP32_S3_CHIP:
                    raise ValueError(f"{name} is not an ESP32-S3 image")
            if manifest_name == "manifest.json" and merged[offset:offset + len(data)] != data:
                raise ValueError(f"{name} differs from the original Pocket Nexus merged image")
            destination = site / "firmware" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            previous_end = end
            print(f"{offset:#07x}: {name} ({len(data)} bytes)")

    shutil.copyfile(pocket_merged, site / "firmware/pocket-nexus.bin")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path, default=Path("site"))
    parser.add_argument("--pio-build", type=Path, default=Path(".pio/build/m5stack-sticks3"))
    parser.add_argument("--boot-app0", type=Path, required=True)
    parser.add_argument("--pocket-merged", type=Path, default=Path("dist/pocket-nexus.bin"))
    parser.add_argument("--page-build", type=Path, default=Path("firmware/page-turner/build"))
    args = parser.parse_args()
    prepare(args.site, args.pio_build, args.boot_app0, args.pocket_merged, args.page_build)
