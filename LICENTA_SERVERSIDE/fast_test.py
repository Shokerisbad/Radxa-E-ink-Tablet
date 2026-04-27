import sys
from pathlib import Path
sys.path.append('d:/LICENTA/LICENTA_SERVERSIDE')

print('Step 1: Importing Candidate Generator...')
try:
    from src.recommender.candidate_generator import CandidateGenerator
    print('SUCCESS: Candidate Generator imported.')
except Exception as e:
    print('ERROR:', e)

print('Step 2: Importing Main...')
try:
    from src.main import initialize_system
    print('SUCCESS: Main imported.')
except Exception as e:
    print('ERROR:', e)

print('Step 3: Running initialize_system()...')
try:
    gen = initialize_system()
    print('SUCCESS: System initialized. FAISS loaded.')
    print('FAISS Shape:', gen.index.ntotal)
except Exception as e:
    print('ERROR:', e)

print('All diagnostics complete.')
