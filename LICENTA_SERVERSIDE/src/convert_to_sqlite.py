import pyarrow.parquet as pq
import sqlite3
from pathlib import Path
import sys

# Add project root to path
sys.path.append(str(Path(__file__).resolve().parent.parent))
from src.config import MAPPING_DF_PATH

SQLITE_PATH = Path("D:/LICENTA/LICENTA_SERVERSIDE/data/book_mapping.db")

def convert():
    if SQLITE_PATH.exists():
        SQLITE_PATH.unlink()
        
    conn = sqlite3.connect(SQLITE_PATH)
    cur = conn.cursor()
    # Using FTS5 (Full Text Search) to construct a BM25 Inverted Index on the fly!
    # UNINDEXED prevents SQLite from wasting time indexing URLs and numbers.
    cur.execute('''CREATE VIRTUAL TABLE books USING fts5(
        faiss_id UNINDEXED,
        book_id UNINDEXED,
        title,
        average_rating UNINDEXED,
        description,
        publication_year UNINDEXED,
        num_pages UNINDEXED,
        image_url UNINDEXED,
        tags,
        language_code UNINDEXED
    )''')
    
    print("Streaming Parquet -> SQLite (Zero RAM Overhead)...")
    parquet_file = pq.ParquetFile(MAPPING_DF_PATH)
    
    faiss_id = 0
    for batch in parquet_file.iter_batches(batch_size=50000):
        # Bypass pandas entirely to avoid string conversion deadlocks on 2M rows
        d = batch.to_pydict()
        
        book_ids = d['book_id']
        titles = d['title']
        ratings = d['average_rating']
        descriptions = d['description']
        years = d.get('publication_year', [''] * len(book_ids))
        pages = d.get('num_pages', [''] * len(book_ids))
        images = d.get('image_url', [''] * len(book_ids))
        tags = d.get('tags', [''] * len(book_ids))
        language_codes = d.get('language_code', [''] * len(book_ids))
        
        records = []
        for i in range(len(book_ids)):
            desc = descriptions[i]
            if desc is None: 
                desc = ""
            desc = str(desc)[:250]
            
            rat = ratings[i]
            if rat is None: 
                rat = 0.0
                
            records.append((
                faiss_id, 
                str(book_ids[i]), 
                str(titles[i]), 
                float(rat), 
                desc,
                str(years[i]) if years[i] is not None else "",
                str(pages[i]) if pages[i] is not None else "",
                str(images[i]) if images[i] is not None else "",
                str(tags[i]) if tags[i] is not None else "",
                str(language_codes[i]) if language_codes[i] is not None else ""
            ))
            faiss_id += 1
            
        cur.executemany("INSERT INTO books VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", records)
        conn.commit()
        print(f"Processed {faiss_id} rows...")
        
    conn.close()
    print("Done! SQLite DB is ready.")

if __name__ == "__main__":
    convert()
