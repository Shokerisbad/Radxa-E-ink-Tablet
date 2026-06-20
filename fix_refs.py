import os

tex_file = r'd:\LICENTA\Licenta DOC\licenta.tex'
with open(tex_file, 'r', encoding='utf-8') as f:
    content = f.read()

# I will define the exact strings that were mistakenly written.
# Since I used r'...' in python, the string in the file literally contains '\u00ee', '\n', '\t'
mistakes = [
    (
        r'sarcini electrice de semn opus, cum se poate vedea \u00een figura \\ref{fig:einkparticle}.\n\t\\begin{figure} [H]',
        'sarcini electrice de semn opus\n\t\\begin{figure} [H]'
    ),
    (
        r'utilizarea modulelor cu controler integrat, dup\u0103 cum se poate vedea \u00een figura \\ref{fig:radxa}.\n\t\n\t\\begin{figure}',
        'utilizarea modulelor cu controler integrat.\n\t\n\t\\begin{figure}'
    ),
    (
        r'la nivelul magistralelor. O ilustrare a acestui modul se poate vedea \u00een figura \\ref{fig:display}.\n\t\n\t\\begin{figure}',
        'la nivelul magistralelor.\n\t\n\t\\begin{figure}'
    ),
    (
        r'dic\u021bionar contextual. Aceast\u0103 interfa\u021b\u0103 se poate vedea \u00een figura \\ref{fig:library_ui}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}',
        'dic\u021bionar contextual.\n\t\\end{itemize}\n\t\n\t\\begin{figure}'
    ),
    (
        r'randare final\u0103. R\u0103spunsul asistentului se poate vedea \u00een figura \\ref{fig:ai_assistant}.\n\t\\end{enumerate}\n\t\n\t\\begin{figure}',
        'randare final\u0103.\n\t\\end{enumerate}\n\t\n\t\\begin{figure}'
    ),
    (
        r'erorilor de logic\u0103. Acest simulator se poate vedea \u00een figura \\ref{fig:windows_simulator}.\n\t\n\t\\begin{figure}',
        'erorilor de logic\u0103.\n\t\n\t\\begin{figure}'
    ),
    (
        r'directorul dedicat bibliotecii. Informa\u021biile expuse se pot vedea \u00een figura \\ref{fig:web_dashboard}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}',
        'directorul dedicat bibliotecii.\n\t\\end{itemize}\n\t\n\t\\begin{figure}'
    ),
    (
        r'recomandarea prezent\u0103). Aceast\u0103 arhitectur\u0103 se poate vedea \u00een figura \\ref{fig:recsys_pipeline}.\n\t\n\t\\begin{figure}',
        'recomandarea prezent\u0103).\n\t\n\t\\begin{figure}'
    ),
    (
        r'scalarea backend-ului. Propunerea arhitectural\u0103 se poate vedea \u00een figura \\ref{fig:cloud_architecture}.\n\t\n\t\\begin{figure}',
        'scalarea backend-ului.\n\t\n\t\\begin{figure}'
    )
]

new_content = content
for i, (bad, good) in enumerate(mistakes):
    if bad in new_content:
        new_content = new_content.replace(bad, good)
        print(f"Fixed mistake {i+1}")
    else:
        print(f"Mistake {i+1} not found")

if new_content != content:
    with open(tex_file, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("File restored successfully.")
else:
    print("No changes needed.")
