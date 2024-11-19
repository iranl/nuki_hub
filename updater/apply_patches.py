from os.path import join, isfile, isdir

Import("env")

FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-espidf")
patchflag_path = join(FRAMEWORK_DIR, ".patching-done")

# patch file only if we didn't do it before
if not isfile(join(FRAMEWORK_DIR, ".patching-done")):
    original_file = join(FRAMEWORK_DIR, "components", "bt", "host", "nimble", "nimble")
    patched_file = join("..", "patches", "remove_min_max_defs.patch")
    
    assert isdir(original_file) and isfile(patched_file)

    env.Execute("git apply --directory %s %s" % (original_file, patched_file))
    # env.Execute("touch " + patchflag_path)

    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))