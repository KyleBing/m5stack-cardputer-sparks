# 构建前将 assets/ 同步到 data/，供 LittleFS 打包
Import("env")

import os
import shutil

project_dir = env["PROJECT_DIR"]
src = os.path.join(project_dir, "assets")
dst = os.path.join(project_dir, "data", "assets")

if os.path.isdir(src):
    shutil.copytree(src, dst, dirs_exist_ok=True)
