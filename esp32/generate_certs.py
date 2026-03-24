"""
Pre-build script: generate stub .S files for embedded certificates.
PlatformIO's scons doesn't process cmake's target_add_binary_data,
so we generate stub assembly files to satisfy the linker.
We don't use ESP Insights or RainMaker - these are just placeholders.
"""
import os

Import("env")

build_dir = env.subst("$BUILD_DIR")

# All certs needed by managed components we don't use
cert_stubs = [
    "https_server.crt",
    "mqtt_server.crt",
    "rmaker_mqtt_server.crt",
    "rmaker_claim_service_server.crt",
    "rmaker_ota_server.crt",
]

for name in cert_stubs:
    asm_path = os.path.join(build_dir, f"{name}.S")
    if os.path.exists(asm_path):
        continue

    sym = name.replace(".", "_").replace("-", "_")
    os.makedirs(build_dir, exist_ok=True)
    with open(asm_path, "w") as f:
        f.write(f'.section .rodata.embedded\n')
        f.write(f'.global _binary_{sym}_start\n')
        f.write(f'.type _binary_{sym}_start, @object\n')
        f.write(f'.align 4\n')
        f.write(f'_binary_{sym}_start:\n')
        f.write(f'.byte 0\n')
        f.write(f'_binary_{sym}_end:\n')
        f.write(f'.global _binary_{sym}_end\n')

    print(f"Generated stub: {asm_path}")
