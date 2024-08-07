""" PlatformIO POST script execution to copy updater """

Import("env")
import glob
import os
import shutil
from pathlib import Path

def get_board_name(env):
    board = env.get('BOARD_MCU')
    return board

def create_target_dir(env):
    board = get_board_name(env)
    target_dir = env.GetProjectOption("custom_build") + '/' + board
    if not os.path.exists(target_dir):
        os.makedirs(target_dir)
    return target_dir

def copy_files(source, target, env):
    file = Path(target[0].get_abspath())
    target_dir = create_target_dir(env)
    board = get_board_name(env)

    if "firmware" in file.stem:
        shutil.copy(file, f"{target_dir}/updater.bin")

def remove_files(source, target, env):
    for f in glob.glob("src/*.cpp"):
        os.remove(f)
        
    for f in glob.glob("src/*.h"):
        os.remove(f)
        
    for f in glob.glob("src/networkDevices/*.cpp"):
        os.remove(f)
        
    for f in glob.glob("src/networkDevices/*.h"):
        os.remove(f)

env.AddPostAction("$BUILD_DIR/firmware.bin", copy_files)
env.AddPostAction("$BUILD_DIR/firmware.bin", remove_files)
