import os
from pathlib import Path

# Base Paths
PROJECT_ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = PROJECT_ROOT / "data"

# Dataset Paths
BOOKS_DATA_PATH = DATA_DIR / "goodreads_books.json.gz"
INTERACTIONS_DATA_PATH = DATA_DIR / "goodreads_interactions.csv"

# Recommender Parameters
FAISS_INDEX_PATH = DATA_DIR / "faiss_book_index.bin"
MAPPING_DF_PATH = DATA_DIR / "book_mapping.parquet"  # Fast loading map of Book ID -> Index

# Filtering (Stage 1) Hyperparameters
TOP_K_CANDIDATES = 20

# Ollama Reranking (Stage 2) Hyperparameters
OLLAMA_MODEL = "mistral-nemo"  # Adjusted based on local model
OLLAMA_BASE_URL = "http://localhost:11434"

# Ensure data directory exists
os.makedirs(DATA_DIR, exist_ok=True)
