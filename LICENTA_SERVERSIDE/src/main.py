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
            latest = liked_books[-1]
            others = liked_books[:-1][-2:] # Up to 2 other recent books
            if others:
                liked_context = f"'{latest}' (most recent), as well as {', '.join(others)}"
            else:
                liked_context = f"'{latest}'"
            full_query = f"The user recently loved {liked_context}. Find semantic matches for their new search: {user_profile}"
    
    if exact_match:
        # EXACT BM25 KEYWORD SEARCH (SQLite FTS5)
        print(f"\n[FTS5 Engine] Querying SQLite BM25 for Exact Matches: '{user_profile}'")
        conn = sqlite3.connect(DATA_DIR / "book_mapping.db")
        
        # Wrap query in quotes for FTS5 phrase matching
        safe_query = user_profile.replace('"', '')
        fts_query = f'"{safe_query}"'
        
        # FTS5 rank is mathematically negative (lower is better), which perfectly mimics Chroma L2 distance!
        df_query = """
            SELECT book_id as id, title, average_rating, ratings_count, image_url, description, tags, publication_year, num_pages, rank as similarity_score
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
        
    # 3. Hybrid Semantic-Popularity Sort / Chronological Search Sort
    if exact_match:
        # Bypass Bayesian Math and Chroma for Exact Matches.
        # Prioritize books where the search term is directly in the title, then sort chronologically!
        print("[Engine] Bypassing Semantic Ranking to preserve chronological series order.")
        safe_query_lower = user_profile.replace('"', '').lower()
        candidates_df['title_match'] = candidates_df['title'].str.lower().str.contains(safe_query_lower, na=False)
        candidates_df['publication_year'] = pd.to_numeric(candidates_df['publication_year'], errors='coerce').fillna(9999)
        candidates_df = candidates_df.sort_values(by=['title_match', 'publication_year'], ascending=[False, True])
    else:
        # We prioritize the exact semantic match (L2 Distance: lower is better) but apply a distance reduction bonus 
        # to highly rated books and a distance penalty to low rated books, using a Bayesian Average.
        if 'average_rating' in candidates_df.columns and 'similarity_score' in candidates_df.columns and 'ratings_count' in candidates_df.columns:
            candidates_df['average_rating'] = pd.to_numeric(candidates_df['average_rating'], errors='coerce').fillna(0.0)
            candidates_df['ratings_count'] = pd.to_numeric(candidates_df['ratings_count'], errors='coerce').fillna(0)
            
            # Bayesian Average: C = 3.8 (dataset mean rating), m = 1000 (min votes to be confident)
            C = 3.8
            m = 1000.0
            
            v = candidates_df['ratings_count']
            R = candidates_df['average_rating']
            
            bayesian_rating = (v / (v + m)) * R + (m / (v + m)) * C
            
            # Mean-Centered Penalty: Center the rating around 3.8
            centered_rating = bayesian_rating - C
            
            # A positive centered_rating (good book) reduces distance (makes it better).
            # A negative centered_rating (bad book) increases distance (penalizes it).
            candidates_df['hybrid_score'] = candidates_df['similarity_score'] - (centered_rating / 1.2) * 0.15
            
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
    if exact_match:
        # Bypass LLM for instant search results
        print("[Engine] Bypassing LLM Reranker for instant Search results.")
        final_results = []
        for _, row in final_candidates.iterrows():
            r = row.to_dict()
            r['id'] = str(r.get('id', r.get('book_id', '')))
            r['reasoning'] = "" # Skip LLM generation
            
            img_val = r.get('image_url', '')
            if pd.isna(img_val) or str(img_val).strip().lower() in ['nan', 'none']:
                r['image_url'] = ''
            else:
                r['image_url'] = str(img_val)
            final_results.append(r)
        return final_results
    else:
        from src.recommender.llm_reranker import LLMReranker
        reranker = LLMReranker()
        try:
            reranked_results = reranker.rerank(user_profile, final_candidates, finished_books)
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