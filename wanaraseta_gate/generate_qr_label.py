import os
import itertools
import string
import qrcode
from PIL import Image

def create_qr_label(data, folder_path):
    # Generate QR Code
    qr = qrcode.QRCode(
        version=1,
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=10,
        border=1, # Border minimal untuk memaksimalkan ruang
    )
    qr.add_data(data)
    qr.make(fit=True)
    img_qr = qr.make_image(fill_color="black", back_color="white")

    # Resize QR code ke resolusi persegi (300x300 px) agar tajam
    img_qr = img_qr.resize((300, 300), Image.Resampling.LANCZOS)

    # Simpan gambar PNG
    file_path = os.path.join(folder_path, f"{data}.png")
    img_qr.save(file_path, dpi=(300, 300))

if __name__ == "__main__":
    mmyy = input("Masukkan format MMYY (contoh: 0626): ")
    
    try:
        limit = int(input("Berapa banyak QR code yang ingin dibuat? (Maksimal 67600): "))
    except ValueError:
        print("Harap masukkan angka yang valid!")
        exit()

    if limit > 67600:
        print("Maksimal kombinasi untuk format 2 Huruf + 2 Angka adalah 67.600. Mengatur nilai ke maksimal...")
        limit = 67600

    folder_name = f"qr{mmyy}"
    os.makedirs(folder_name, exist_ok=True)
    print(f"\nMembuat {limit} QR code di folder '{folder_name}'...\n(Proses ini mungkin memakan waktu beberapa saat jika Anda mencetak dalam jumlah sangat besar)")

    # Generator iterasi kombinasi [A-Z][A-Z][0-9][0-9]
    # Total iterasi: 26 * 26 * 10 * 10 = 67.600
    letters = string.ascii_uppercase
    digits = string.digits
    combinations = itertools.product(letters, letters, digits, digits)

    count = 0
    for combo in combinations:
        if count >= limit:
            break
        
        # Membentuk susunan contoh: ws-0626AB12
        suffix = "".join(combo)
        data = f"ws-{mmyy}{suffix}"
        
        create_qr_label(data, folder_name)
        count += 1
        
        # Print progres setiap 5000 item agar log console terminal tidak "lag"
        if count % 5000 == 0:
            print(f"Progress: {count} / {limit} QR codes selesai dibuat...")

    print(f"\nSelesai! {count} file .png berhasil disimpan di folder '{folder_name}'.")