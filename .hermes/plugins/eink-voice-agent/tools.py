import json
import subprocess
import os

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FIRMWARE_DIR = os.path.join(PROJECT_DIR, "firmware")
IDF_PATH = "/opt/esp-idf"


def _source_idf(cmd):
    return f". {IDF_PATH}/export.sh && cd {FIRMWARE_DIR} && {cmd}"


def build_firmware(args, **kwargs) -> str:
    try:
        result = subprocess.run(
            ["bash", "-c", _source_idf("idf.py build 2>&1")],
            capture_output=True, text=True, timeout=300,
        )
        lines = result.stdout.splitlines()
        tail = "\n".join(lines[-20:])
        return json.dumps({
            "success": result.returncode == 0,
            "output": tail,
        })
    except subprocess.TimeoutExpired:
        return json.dumps({"success": False, "output": "Build timed out after 5 minutes"})
    except Exception as e:
        return json.dumps({"success": False, "output": f"Error: {e}"})


def flash_firmware(args, **kwargs) -> str:
    port = args.get("port", "/dev/ttyUSB0")
    try:
        result = subprocess.run(
            ["bash", "-c", _source_idf(f"idf.py -p {port} flash 2>&1")],
            capture_output=True, text=True, timeout=120,
        )
        return json.dumps({
            "success": result.returncode == 0,
            "output": result.stdout[-1000:] if result.stdout else result.stderr[-1000:],
        })
    except Exception as e:
        return json.dumps({"success": False, "output": f"Error: {e}"})


def monitor_device(args, **kwargs) -> str:
    port = args.get("port", "/dev/ttyUSB0")
    return json.dumps({
        "info": f"Run manually: cd {FIRMWARE_DIR} && idf.py -p {port} monitor",
    })


def ci_status(args, **kwargs) -> str:
    try:
        result = subprocess.run(
            ["gh", "run", "list", "--repo", "tingtom/eink-voice-agent",
             "--branch", "main", "--limit", "3", "--json", "conclusion,displayTitle,status,createdAt"],
            capture_output=True, text=True, timeout=15,
        )
        if result.returncode != 0:
            return json.dumps({"success": False, "output": "gh CLI not available or not authenticated"})
        return json.dumps({"success": True, "runs": result.stdout})
    except Exception as e:
        return json.dumps({"success": False, "output": f"Error: {e}"})
