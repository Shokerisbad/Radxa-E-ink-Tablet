import json
import gzip
import pandas as pd
from typing import Generator, Dict, Any

from src.config import BOOKS_DATA_PATH, INTERACTIONS_DATA_PATH

def load_books_metadata(filepath: str = BOOKS_DATA_PATH, min_rating_threshold: int = 500) -> pd.DataFrame:
    """
    Loads book metadata from the gzipped JSON file.
    Because the file is large, we can process it line-by-line.
    Returns a DataFrame containing basic features needed for recommendation.
    """
    books = []
    
    # Check if file exists to prevent hard crash
    if not filepath.exists():
        print(f"Warning: Book dataset not found at {filepath}. Please download it.")
        return pd.DataFrame()

    from tqdm import tqdm
    
    with gzip.open(filepath, 'rt', encoding='utf-8') as f:
        print(f"Loading books from {filepath} (Filtering uniquely for >= {min_rating_threshold} ratings)...")
        for idx, line in tqdm(enumerate(f), desc="Parsing Dataset"):
            try:
                # The goodreads datasets have multiple JSON objects separated by newlines
                b = json.loads(line.strip())
                
                # Fast exclusion filter (Saves 99% of memory)
                rc = int(b.get('ratings_count', 0))
                if rc < min_rating_threshold:
                    continue
                
                # Extract top 5 user tags
                shelves = b.get('popular_shelves', [])
                top_tags = [shelf['name'] for shelf in shelves[:5]]
                tags_str = " ".join(top_tags)
                
                books.append({
                    'book_id': b.get('book_id'),
                    'title': b.get('title'),
                    'average_rating': float(b.get('average_rating', 0)),
                    'ratings_count': rc,
                    'description': b.get('description', ''),
                    'num_pages': b.get('num_pages'),
                    'publication_year': b.get('publication_year'),
                    'image_url': b.get('image_url', ''),
                    'tags': tags_str
                })
            except Exception as e:
                continue

    df = pd.DataFrame(books)
    # Filter out entries with no title or rating
    df = df.dropna(subset=['title'])
    df['average_rating'] = df['average_rating'].fillna(0)
    return df

def load_interactions(filepath: str = INTERACTIONS_DATA_PATH, sample_fraction: float = 0.01) -> pd.DataFrame:
    """
    Loads user-book interactions. The full CSV is 4GB+.
    We read it in chunks or take a sample for prototyping.
    """
    if not filepath.exists():
        print(f"Warning: Interactions dataset not found at {filepath}. Please download it.")
        return pd.DataFrame()
        
    print(f"Loading interactions sample ({sample_fraction*100}%)...")
    # For a 4GB file, loading the whole thing directly might crash a laptop. 
    # Use chunking.
    chunks = pd.read_csv(filepath, chunksize=100_000, 
                         usecols=['user_id', 'book_id', 'is_read', 'rating'])
    
    sampled_chunks = []
    for chunk in chunks:
        # Take a random sample of the chunk
        sampled_chunks.append(chunk.sample(frac=sample_fraction, random_state=42))
        
    df = pd.concat(sampled_chunks, ignore_index=True)
    return df
