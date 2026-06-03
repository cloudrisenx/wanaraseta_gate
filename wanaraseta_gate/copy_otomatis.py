Import("env")
import os
import shutil

def copy_bin(source, target, env):
    # Ambil lokasi file hasil build
    bin_path = str(source[0].get_abspath())
    
    # Dapatkan path lengkap dari folder src yang sedang aktif (otomatis baca dari [platformio])
    target_path = env.subst("$PROJECT_SRC_DIR")
    
    # Ambil nama foldernya saja untuk keperluan log print
    nama_folder = os.path.basename(os.path.normpath(target_path))
    
    # Jaga-jaga kalau foldernya belum ada
    if not os.path.exists(target_path):
        os.makedirs(target_path)
        
    target_file = os.path.join(target_path, "firmware.bin")
    
    # Lakukan copy
    shutil.copyfile(bin_path, target_file)
    
    print("\n=======================================================")
    print(f"✅ AUTO-COPY SUKSES BOSS!")
    print(f"File hasil build langsung dimasukkan ke folder: {nama_folder}/firmware.bin")
    print("=======================================================\n")

# Jalankan otomatis setelah build selesai
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin)