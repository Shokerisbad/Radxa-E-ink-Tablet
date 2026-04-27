import sys
import pandas as pd
import sqlite3
from pathlib import Path

# Fix module loading so we can import from src
sys.path.append(str(Path(__file__).resolve().parent.parent))

from src.dataset_loader import load_books_metadata
from src.recommender.candidate_generator import CandidateGenerator
from src.config import TOP_K_CANDIDATES, DATA_DIR

def initialize_system():
    """Initializes and builds the Chroma Vector index if it doesn't exist."""
    db_path = DATA_DIR / "chroma_db"
    generator = CandidateGenerator(str(db_path))
    
    if not db_path.exists():
        print("First run detected. Building the pure Semantic Vector database...")
        generator.build_index()
    else:
        print("Loading existing Chroma vector database...")
        
    return generator

def recommend(user_profile: str, generator: CandidateGenerator, session_history: list, rating_pref: float = 4.0, finished_books: list = None, use_reviews: bool = False):
    """
    Runs the full recommendation pipeline:
    1. Query ChromaDB with pure semantic embeddings using Ollama.
    2. Filter Top K matched candidates and sort them strictly by Goodreads rating.
    """
    if finished_books is None:
        finished_books = []
    
    print(f"\n======== STARTING PIPELINE ========\nUser Profile: '{user_profile}' | Use Reviews: {use_reviews}")
    
    # 1. Expand query context dynamically based on UI preferences
    full_query = user_profile
    
    if use_reviews and finished_books:
        # Only inject books that the user actively reviewed positively 
        liked_books = [b.get("title", "") for b in finished_books if b.get("user_rating", 0.0) >= rating_pref]
        if liked_books:
            liked_context = ", ".join(liked_books)
            full_query = f"User highly rated these books: {liked_context}. Find semantic matches for their new search: {user_profile}"
    
    if not use_reviews:
        # EXACT BM25 KEYWORD SEARCH (SQLite FTS5)
        print(f"\n[FTS5 Engine] Querying SQLite BM25 for Exact Matches: '{user_profile}'")
        conn = sqlite3.connect(DATA_DIR / "book_mapping.db")
        
        # Wrap query in quotes for FTS5 phrase matching
        safe_query = user_profile.replace('"', '')
        fts_query = f'"{safe_query}"'
        
        # FTS5 rank is mathematically negative (lower is better), which perfectly mimics Chroma L2 distance!
        df_query = """
            SELECT book_id as id, title, average_rating, image_url, rank as similarity_score
            FROM books
            WHERE books MATCH ?
            ORDER BY rank
            LIMIT 50
        """
        try:
            candidates_df = pd.read_sql_query(df_query, conn, params=(fts_query,))
        except Exception as e:
            print(f"[FTS5 Warning] Query failed (possibly invalid syntax): {e}")
            candidates_df = pd.DataFrame()
        finally:
            conn.close()
    else:
        # 2. Candidate Generation (Instant Semantic Vector Search)
        print(f"\n[Chroma Engine] Querying ChromaDB for Semantic Vectors related to: '{full_query}'")
        candidates_df = generator.generate_candidates(full_query, top_k=50)
    
    if candidates_df.empty:
        print("[Warning] No candidates found! Did the vector database build correctly?")
        return []
        
    # 3. Hybrid Semantic-Popularity Sort
    # The pure 'average_rating' sort was bubbling unrelated books to the top just because they were 5-stars.
    # We now prioritize the exact semantic match (L2 Distance: lower is better) but give a slight distance reduction bonus to highly rated books.
    if 'average_rating' in candidates_df.columns and 'similarity_score' in candidates_df.columns:
        candidates_df['average_rating'] = candidates_df['average_rating'].apply(lambda x: float(x) if pd.notnull(x) else 0.0)
        
        # A 5.0 rating gives a -0.15 distance bonus, allowing famous books to edge out obscure books IF they are semantically tied.
        candidates_df['hybrid_score'] = candidates_df['similarity_score'] - (candidates_df['average_rating'] / 5.0) * 0.15
        
        candidates_df = candidates_df.sort_values(by='hybrid_score', ascending=True)
        
    # 4. Deduplication
    # Goodreads contains hundreds of duplicate entries for the exact same book (Hardcover, Kindle, Audiobook, Translations).
    # We drop any duplicates matching the exact same title to prevent flooding the UI.
    if 'title' in candidates_df.columns:
        candidates_df = candidates_df.drop_duplicates(subset=['title'], keep='first')
        
    final_candidates = candidates_df.head(TOP_K_CANDIDATES)
    
    print(f"\n=== Final Semantic Results (Top {TOP_K_CANDIDATES}) ===")
    print(final_candidates[['title', 'similarity_score', 'average_rating']].to_string(index=False).encode('utf-8', 'ignore').decode('utf-8'))
    
    # Extract records directly. No LLM JSON decoding required!
    return final_candidates.to_dict('records')

if __name__ == "__main__":
    print("\n================================================")
    print("  Goodreads Semantic Recommender (ChromaDB)     ")
    print("================================================")
    
    print("Initializing system...")
    try:
        generator = initialize_system()
    except Exception as e:
        print(f"Failed to initialize: {e}")
        sys.exit(1)
        
    print("\nSystem ready! Type your reading preferences below.")
    print("Type 'exit' or 'quit' to close the program.")
    
    session_history = []
    
    while True:
        try:
            profile = input("\n> What kind of book are you looking for? \n> ")
            
            if profile.lower().strip() in ['exit', 'quit']:
                print("Goodbye!")
                break
                
            if not profile.strip():
                continue
                
            recommend(profile, generator, session_history)
            
            session_history.append(profile)
            if len(session_history) > 3:
                session_history.pop(0)
            
        except KeyboardInterrupt:
            print("\nExiting...")
            break