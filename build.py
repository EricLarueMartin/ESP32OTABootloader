"""Pinned Arduino build; outputs and generated sketch copies stay under build/."""
import argparse, ctypes, hashlib, json, os, shutil, subprocess
from pathlib import Path
P = Path(__file__).resolve().parent

def short(path):
    path = path.resolve()
    if os.name != "nt": return str(path)
    buf = ctypes.create_unicode_buffer(32768)
    if not ctypes.windll.kernel32.GetShortPathNameW(str(path), buf, len(buf)):
        raise RuntimeError("Cannot resolve Windows short path")
    return buf.value

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile", choices=["recovery", "selftest", "badboot", "badloop"])
    ap.add_argument("--board", choices=["huzzah32", "xiao-c6"], default="huzzah32")
    ap.add_argument("--config", type=Path, required=True)
    ap.add_argument("--sdk", type=Path, required=True)
    ap.add_argument("--version", default="0.1.0-dev")
    ap.add_argument("--cli", default="arduino-cli")
    a = ap.parse_args()
    import re
    if not re.fullmatch(r"[A-Za-z0-9._+-]{1,63}", a.version):raise ValueError("Invalid release version")
    name = "recovery" if a.profile == "recovery" else "selftest"
    out = P / "build" / a.board / a.profile
    sketch = out / "sketch" / name
    sketch.mkdir(parents=True, exist_ok=True)
    for file in (P / name).glob("*"):
        if file.is_file(): shutil.copy2(file, sketch / file.name)
    shutil.copy2(P / "arduino_startup.h", sketch / "arduino_startup.h")
    if name == "selftest":
        for file in ["update_state.h", "identity.h", "authentication.h"]:
            shutil.copy2(P / "recovery" / file, sketch / file)
        shutil.copy2(P / "application_entry.h", sketch / "application_entry.h")
    binary = out / "binary"
    binary.mkdir(exist_ok=True)
    fqbn = {"huzzah32": "esp32:esp32:featheresp32", "xiao-c6": "esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc"}[a.board]
    limit = 0x100000 if name == "recovery" else 0x2c0000
    args = [a.cli, "compile", "--config-file", str(a.config.resolve()), "--fqbn", fqbn,
        "--build-property", "tools.esp32-arduino-libs.path=" + short(a.sdk),
        "--build-property", "compiler.c.elf.extra_flags=-Wl,--wrap=initArduino,--wrap=esp_wifi_init",
        "--build-property", "upload.maximum_size=" + str(limit), "--jobs", "4", "--build-path", short(binary)]
    # Keep the Windows multilib workaround in this project, not shared platform.txt.
    if a.board == "huzzah32":
        toolchain = a.sdk.parent.parent / "esp-x32" / "2511"
        tool_property = "tools.xtensa-esp-elf-gcc.path="
        relative_include = "/xtensa-esp-elf/include/c++/14.2.0/xtensa-esp-elf/esp32/no-rtti"
    else:
        toolchain = a.sdk.parent.parent / "esp-rv32" / "2511"
        tool_property = "tools.riscv32-esp-elf-gcc.path="
        relative_include = "/riscv32-esp-elf/include/c++/14.2.0/riscv32-esp-elf/rv32imac_zicsr_zifencei/ilp32/no-rtti"
    args += ["--build-property", tool_property + short(toolchain)]
    includes = short(toolchain) + relative_include
    args += ["--build-property", 'compiler.cpreprocessor.flags="@{compiler.sdk.path}/flags/defines" "-I{build.source.path}" -iprefix "{compiler.sdk.path}/include/" "@{compiler.sdk.path}/flags/includes" "-I{compiler.sdk.path}/{build.memory_type}/include" "-isystem' + includes + '"']
    flags = {"badboot": "-DSELFTEST_FAIL_EARLY=1", "badloop": "-DSELFTEST_HANG_AFTER_HEALTH=1"}
    if a.profile in flags: args += ["--build-property", "compiler.cpp.extra_flags=" + flags[a.profile]]
    r = subprocess.run(args + [str(sketch)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / "compile.log").write_bytes(r.stdout)
    if r.returncode:
        print(r.stdout.decode(errors="replace")[-6000:]); raise SystemExit(r.returncode)
    image = binary / (name + ".ino.bin")
    if image.stat().st_size > limit: raise RuntimeError("Partition overflow")
    if "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in (binary / "sdkconfig").read_text():
        raise RuntimeError("Bootloader rollback required")
    receipt = dict(board=a.board, profile=a.profile, image_bytes=image.stat().st_size,
        image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(), hardware_qualified=False,
        sources={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sketch.iterdir() if f.is_file()})
    manifest=dict(name="esp32-ota-"+a.profile, version=a.version, sha256=receipt["image_sha256"], size=receipt["image_bytes"])
    image.with_suffix(".manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({k:v for k,v in receipt.items() if k != "sources"}))
    print(r.stdout.decode(errors="replace")[-1200:])
if __name__ == "__main__": main()
