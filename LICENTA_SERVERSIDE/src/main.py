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
    """Initializes and builds the Chroma Vector index and SQLite DB if they don't exist."""
    db_path = DATA_DIR / "chroma_db"
    sqlite_path = DATA_DIR / "book_mapping.db"
    generator = CandidateGenerator(str(db_path))
    
    if not db_path.exists() or not sqlite_path.exists():
        print("First run detected. Parsing JSON and building databases...")
        from src.config import MAPPING_DF_PATH
        
        # 1. Parse JSON -> DataFrame
        df = load_books_metadata()
        
        # 2. Save intermediate Parquet for SQLite
        df.to_parquet(MAPPING_DF_PATH)
        
        # 3. Build ChromaDB
        generator.books_df = df
        generator.build_index()
        
        # 4. Build SQLite FTS5
        import src.convert_to_sqlite as sqlite_builder
        sqlite_builder.convert()
    else:
        print("Loading existing databases...")
        
    return generator

def recommend(user_profile: str, generator: CandidateGenerator, session_history: list, rating_pref: float = 4.0, finished_books: list = None, use_reviews: bool = False, target_language: str = 'eng', exact_match: bool = False):
    """
    Runs the full recommendation pipeline:
    1. Query ChromaDB with pure semantic embeddings using Ollama.
    2. Filter Top K matched candidates and sort them strictly by Goodreads rating.
    """
    if finished_books is None:
        finished_books = []
    
    print(f"\n======== STARTING PIPELINE ========\nUser Profile: '{user_profile}' | Use Reviews: {use_reviews} | Language: {target_language} | Exact Match: {exact_match}")
    
    # 1. Expand query context dynamically based on UI preferences
    full_query = user_profile
    
    if use_reviews and finished_books:
        # Only inject books that the user actively reviewed positively 
        liked_books = [b.get("title", "") for b in finished_books if b.get("user_rating", 0.0) >= rating_pref]
        if liked_books:
            liked_context = ", ".join(liked_books)
            full_query = f"User highly rated these books: {liked_context}. Find semantic matches for their new search: {user_profile}"
    
    if exact_match:
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
            WHERE books MATCH ? AND (language_code = ? OR language_code = 'en-US' OR language_code = 'en-GB' OR language_code = '')
            ORDER BY rank
            LIMIT 50
        """
        try:
            candidates_df = pd.read_sql_query(df_query, conn, params=(fts_query, target_language))
        except Exception as e:
            print(f"[FTS5 Warning] Query failed (possibly invalid syntax): {e}")
            candidates_df = pd.DataFrame()
        finally:
            conn.close()
    else:
        # 2. Candidate Generation (Instant Semantic Vector Search)
        print(f"\n[Chroma Engine] Querying ChromaDB for Semantic Vectors related to: '{full_query}'")
        candidates_df = generator.generate_candidates(full_query, top_k=50, language_code=target_language)
    
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
    # We strip out trailing series text like '(Metro #1)' to aggressively deduplicate them.
    if 'title' in candidates_df.columns:
        candidates_df['clean_title'] = candidates_df['title'].str.replace(r'\s*\(.*?\)\s*', '', regex=True).str.strip().str.lower()
        candidates_df = candidates_df.drop_duplicates(subset=['clean_title'], keep='first')
        
    final_candidates = candidates_df.head(TOP_K_CANDIDATES)
    
    print(f"\n=== Final Semantic Results (Top {TOP_K_CANDIDATES}) ===")
    print(final_candidates[['title', 'similarity_score', 'average_rating']].to_string(index=False).encode('utf-8', 'ignore').decode('utf-8'))
    
    # 5. LLM Reranking (Semantic Context)
    from src.recommender.llm_reranker import LLMReranker
    reranker = LLMReranker()
    try:
        reranked_results = reranker.rerank(user_profile, final_candidates)
        return reranked_results
    except Exception as e:
        print(f"[LLM Warning] Reranker failed ({e}), falling back to standard results.")
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