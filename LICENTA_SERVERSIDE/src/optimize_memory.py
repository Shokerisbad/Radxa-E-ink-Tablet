import pandas as pd
from pathlib import Path
import os
import sys

# Add project root to path
sys.path.append(str(Path(__file__).resolve().parent.parent))
from src.config import MAPPING_DF_PATH

def optimize_mapping_memory():
    print(f"Loading unoptimized mapping from {MAPPING_DF_PATH}...")
    if not MAPPING_DF_PATH.exists():
        print("Mapping file not found!")
        return

    df = pd.read_parquet(MAPPING_DF_PATH)
    
    print("Truncating descriptions to 250 characters...")
    df['description'] = df['description'].astype(str).str.slice(0, 250)

    print("Saving optimized parquet...")
    df.to_parquet(MAPPING_DF_PATH)
    print("Optimization Complete! Memory usage drops drastically.")

if __name__ == "__main__":
    optimize_mapping_memory()
