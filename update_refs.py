import os
import re

tex_file = r'd:\LICENTA\Licenta DOC\licenta.tex'
with open(tex_file, 'r', encoding='utf-8') as f:
    content = f.read()

replacements = [
    (
        r'sarcini electrice de semn opus\s*\\begin\{figure\} \[H\]',
        r'sarcini electrice de semn opus, cum se poate vedea \u00een figura \\ref{fig:einkparticle}.\n\t\\begin{figure} [H]'
    ),
    (
        r'utilizarea modulelor cu controler integrat\.\s*\\begin\{figure\}',
        r'utilizarea modulelor cu controler integrat, dup\u0103 cum se poate vedea \u00een figura \\ref{fig:radxa}.\n\t\n\t\\begin{figure}'
    ),
    (
        r'la nivelul magistralelor\.\s*\\begin\{figure\}',
        r'la nivelul magistralelor. O ilustrare a acestui modul se poate vedea \u00een figura \\ref{fig:display}.\n\t\n\t\\begin{figure}'
    ),
    (
        r'dic\u021bionar contextual\.\n\t\\end\{itemize\}\s*\\begin\{figure\}',
        r'dic\u021bionar contextual. Aceast\u0103 interfa\u021b\u0103 se poate vedea \u00een figura \\ref{fig:library_ui}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}'
    ),
    (
        r'randare final\u0103\.\n\t\\end\{enumerate\}\s*\\begin\{figure\}',
        r'randare final\u0103. R\u0103spunsul asistentului se poate vedea \u00een figura \\ref{fig:ai_assistant}.\n\t\\end{enumerate}\n\t\n\t\\begin{figure}'
    ),
    (
        r'erorilor de logic\u0103\.\s*\\begin\{figure\}',
        r'erorilor de logic\u0103. Acest simulator se poate vedea \u00een figura \\ref{fig:windows_simulator}.\n\t\n\t\\begin{figure}'
    ),
    (
        r'directorul dedicat bibliotecii\.\n\t\\end\{itemize\}\s*\\begin\{figure\}',
        r'directorul dedicat bibliotecii. Informa\u021biile expuse se pot vedea \u00een figura \\ref{fig:web_dashboard}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}'
    ),
    (
        r'recomandare hibrid\.\s*\\begin\{figure\}',
        r'recomandare hibrid, dup\u0103 cum se poate vedea \u00een figura \\ref{fig:data_pipeline}.\n\t\n\t\\begin{figure}'
    ),
    (
        r'recomandarea prezent\u0103\)\.\s*\\begin\{figure\}',
        r'recomandarea prezent\u0103). Aceast\u0103 arhitectur\u0103 se poate vedea \u00een figura \\ref{fig:recsys_pipeline}.\n\t\n\t\\begin{figure}'
    ),
    (
        r'scalarea backend-ului\.\s*\\begin\{figure\}',
        r'scalarea backend-ului. Propunerea arhitectural\u0103 se poate vedea \u00een figura \\ref{fig:cloud_architecture}.\n\t\n\t\\begin{figure}'
    )
]

new_content = content
for i, (old_pattern, new_text) in enumerate(replacements):
    # Search if pattern exists
    match = re.search(old_pattern, new_content)
    if match:
        new_content = new_content[:match.start()] + new_text + new_content[match.end():]
        print(f"Replacement {i+1} successful.")
    else:
        print(f"Replacement {i+1} failed to match.")
        
if new_content != content:
    with open(tex_file, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("File updated successfully.")
else:
    print("No changes were made to the file.")
