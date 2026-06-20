import os

tex_file = r'd:\LICENTA\Licenta DOC\licenta.tex'
with open(tex_file, 'r', encoding='utf-8') as f:
    content = f.read()

replacements = [
    (
        'sarcini electrice de semn opus\n\t\\begin{figure} [H]',
        'sarcini electrice de semn opus, cum se poate vedea în figura \\ref{fig:einkparticle}.\n\t\\begin{figure} [H]'
    ),
    (
        'utilizarea modulelor cu controler integrat.\n\t\n\t\\begin{figure}[H]',
        'utilizarea modulelor cu controler integrat, după cum se poate vedea în figura \\ref{fig:radxa}.\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'la nivelul magistralelor.\n\t\n\t\\begin{figure}[H]',
        'la nivelul magistralelor, arhitectură ce se poate vedea în figura \\ref{fig:display}.\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'dicționar contextual.\n\t\\end{itemize}\n\t\n\t\\begin{figure}[H]',
        'dicționar contextual. Aspectul bibliotecii se poate vedea în figura \\ref{fig:library_ui}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'randare finală.\n\t\\end{enumerate}\n\t\n\t\\begin{figure}[H]',
        'randare finală. Aspectul acestei interfețe este ilustrat în figura \\ref{fig:ai_assistant}.\n\t\\end{enumerate}\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'erorilor de logică.\n\t\n\t\\begin{figure}[H]',
        'erorilor de logică, proces ce se poate vedea în figura \\ref{fig:windows_simulator}.\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'directorul dedicat bibliotecii.\n\t\\end{itemize}\n\t\n\t\\begin{figure}[H]',
        'directorul dedicat bibliotecii. O privire de ansamblu asupra acestor funcționalități se poate vedea în figura \\ref{fig:web_dashboard}.\n\t\\end{itemize}\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'recomandare hibrid.\n\t\n\t\\begin{figure}[H]',
        'recomandare hibrid, flux ce se poate vedea în figura \\ref{fig:data_pipeline}.\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'recomandarea prezentă).\n\t\n\t\\begin{figure}[H]',
        'recomandarea prezentă). Această arhitectură se poate vedea în figura \\ref{fig:recsys_pipeline}.\n\t\n\t\\begin{figure}[H]'
    ),
    (
        'scalarea backend-ului.\n\t\n\t\\begin{figure}[H]',
        'scalarea backend-ului. Perspectiva de migrare se poate vedea în figura \\ref{fig:cloud_architecture}.\n\t\n\t\\begin{figure}[H]'
    )
]

new_content = content
for i, (old, new) in enumerate(replacements):
    if old in new_content:
        new_content = new_content.replace(old, new)
        print(f"Applied change {i+1}")
    else:
        print(f"Failed to find match for {i+1}")

if new_content != content:
    with open(tex_file, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("Updates saved.")
else:
    print("No changes made.")
