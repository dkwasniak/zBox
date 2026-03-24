"""
Pre-build script: detect sdkconfig.defaults changes and force regeneration.
Based on mirkosertic/ESP32MusicBox approach.
"""
import os
import json

Import("env")

project_dir = env.subst("$PROJECT_DIR")
pio_dir = os.path.join(project_dir, ".pio")
last_build_file = os.path.join(pio_dir, "lastBuild.json")
defaults_file = os.path.join(project_dir, "sdkconfig.defaults")

if not os.path.exists(defaults_file):
    print("No sdkconfig.defaults found, skipping check")
else:
    current_mtime = os.path.getmtime(defaults_file)
    last_mtime = 0

    if os.path.exists(last_build_file):
        with open(last_build_file, "r") as f:
            try:
                data = json.load(f)
                last_mtime = data.get("sdkconfig_defaults_mtime", 0)
            except json.JSONDecodeError:
                pass

    if current_mtime != last_mtime:
        print("sdkconfig.defaults changed, forcing regeneration...")
        for f in os.listdir(project_dir):
            if f.startswith("sdkconfig.") and f != "sdkconfig.defaults":
                path = os.path.join(project_dir, f)
                print(f"  Removing {f}")
                os.remove(path)

        os.makedirs(pio_dir, exist_ok=True)
        with open(last_build_file, "w") as f:
            json.dump({"sdkconfig_defaults_mtime": current_mtime}, f)
