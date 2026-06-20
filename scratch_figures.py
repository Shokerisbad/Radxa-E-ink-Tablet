import sys
import os

tex_file = r'd:\LICENTA\Licenta DOC\licenta.tex'
with open(tex_file, 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if '\\begin{figure}' in line:
        print(f'--- Figure at line {i+1} ---')
        start = max(0, i-4)
        for j in range(start, i+4):
            if j < len(lines):
                # use utf-8 to bytes then replace to avoid console crashes
                try:
                    text = lines[j].strip().encode('ascii', 'ignore').decode('ascii')
                    print(f'{j+1}: {text}')
                except Exception as e:
                    pass
