import struct
import glob
files = glob.glob('books/.cache/pdf_imgs/*.bmp')
for f in files:
    with open(f, 'rb') as file:
        file.seek(18)
        w, h = struct.unpack('<II', file.read(8))
        print(f, 'W:', w, 'H:', h)
