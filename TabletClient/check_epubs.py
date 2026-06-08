import zipfile
import glob
import sys

books = glob.glob(r"d:\LICENTA\TabletClient\lv_port_pc_visual_studio\LvglWindowsSimulator\books\*.epub")
for book in books:
    try:
        z = zipfile.ZipFile(book)
        opfs = [f for f in z.namelist() if f.endswith('.opf')]
        if opfs:
            print(f"Book: {book}")
            content = z.read(opfs[0]).decode('utf-8', errors='replace')
            for line in content.split('\n'):
                if '<dc:creator' in line:
                    print(line.strip())
    except Exception as e:
        print(e)
