#  esp_ipa's prebuilt libesp_ipa.a ships a *placeholder* IPA config object
#  (esp_video_ipa_config.c.obj) that defines esp_ipa_pipeline_get_config() and
#  only knows a dummy "test_apps_dummy" sensor. The build also generates the
#  *real* config (with the selected sensor's tuning, e.g. SC202CS) into esp_ipa's
#  own component archive. Both define the same symbol; under PlatformIO's link
#  order the linker satisfies the reference from the prebuilt placeholder and the
#  generated object is never pulled, so the ISP pipeline controller can't find
#  the sensor config -> auto exposure / white balance never run (dark/green
#  image).
#
#  Fix: just before linking, delete the placeholder object from the prebuilt
#  archive so the only remaining esp_ipa_pipeline_get_config() is the generated
#  one. Runs every build; idempotent (deleting an absent member is a no-op).

import os
import glob
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def _tool(name):
    cand = os.path.join(os.path.dirname(env.subst("$CC")), name)
    return cand if os.path.isfile(cand) else name


def fix_esp_ipa(source, target, env):
    ar = _tool("riscv32-esp-elf-ar")
    pattern = os.path.join(env["PROJECT_DIR"], "managed_components",
                           "espressif__esp_ipa", "lib", "*", "*", "libesp_ipa.a")
    for lib in glob.glob(pattern):
        try:
            members = subprocess.check_output([ar, "t", lib]).decode().split()
        except Exception as e:
            print("[fix_esp_ipa] WARN: cannot read %s: %s" % (lib, e))
            continue
        for m in members:
            if "esp_video_ipa_config" in m:
                subprocess.call([ar, "d", lib, m])
                subprocess.call([ar, "s", lib])  # refresh symbol index
                print("[fix_esp_ipa] removed placeholder %s from %s" % (m, lib))


# Run right before the firmware is linked (after components are resolved).
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", fix_esp_ipa)
