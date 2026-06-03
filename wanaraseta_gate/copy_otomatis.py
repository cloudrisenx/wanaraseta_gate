Import("env")
import os
import shutil

# =========================================================
# 1. INJECT NAMA FOLDER KE C++ (AUTO_FOLDER_NAME)
# =========================================================
# Dapatkan path dari folder src yang sedang aktif di platformio.ini
project_src_path = env.subst("$PROJECT_SRC_DIR")
nama_folder_aktif = os.path.basename(os.path.normpath(project_src_path))

# Inject ke compiler C++ sebagai macro #define AUTO_FOLDER_NAME
env.Append(CPPDEFINES=[
    ("AUTO_FOLDER_NAME", f'\\"{nama_folder_aktif}\\"')
])

# =========================================================
# 2. COPY BINARY OTOMATIS SETELAH BUILD
# =========================================================
def copy_bin(source, target, env):
    # Ambil lokasi file hasil build
    bin_path = str(source[0].get_abspath())
    
    # Jaga-jaga kalau foldernya belum ada
    if not os.path.exists(project_src_path):
        os.makedirs(project_src_path)
        
    target_file = os.path.join(project_src_path, "firmware.bin")
    
    # Lakukan copy
    shutil.copyfile(bin_path, target_file)
    
    print("\n=======================================================")
    print(f"✅ AUTO-COPY SUKSES BOSS!")
    print(f"File hasil build langsung dimasukkan ke folder: {nama_folder_aktif}/firmware.bin")
    print("=======================================================\n")

# Jalankan otomatis setelah build selesai
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin)