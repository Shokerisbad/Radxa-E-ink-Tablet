import sys
import pandas as pd
from pathlib import Path

# Fix module loading so we can import from src
sys.path.append(str(Path(__file__).resolve().parent.parent))

from src.dataset_loader import load_books_metadata
from src.recommender.candidate_generator import CandidateGenerator
from src.recommender.llm_reranker import LLMReranker
from src.config import FAISS_INDEX_PATH, TOP_K_CANDIDATES

def initialize_system():
    """Initializes and builds the FAISS index if it doesn't exist."""
    generator = CandidateGenerator()
    
    if not FAISS_INDEX_PATH.exists():
        print("First run detected. Loading datasets and building FAISS index...")
        # For prototyping, we load a sample. In production, load the whole dataset
        # and handle chunking/memory carefully.
        print("Note: You need to download the actual Goodreads dataset first.")
        print("Falling back to dummy data generation for demonstration if files don't exist.\n")
        
        books_df = load_books_metadata()
        
        if books_df.empty:
            print("[Warning] Datasets missing. Creating some dummy books to show the pipeline...")
            books_df = pd.DataFrame([
                {'book_id': 1, 'title': 'Neuromancer', 'average_rating': 4.5, 'ratings_count': 50000, 'description': 'Classic Cyberpunk hacker story.'},
                {'book_id': 2, 'title': 'Dune', 'average_rating': 4.7, 'ratings_count': 100000, 'description': 'Epic Sci-Fi about desert planet Arrakis and spice melange.'},
                {'book_id': 3, 'title': 'The Final Empire', 'average_rating': 4.6, 'ratings_count': 60000, 'description': 'High fantasy heist with unique magic system.'},
                {'book_id': 4, 'title': 'Snow Crash', 'average_rating': 4.3, 'ratings_count': 40000, 'description': 'Fast-paced Cyberpunk with virtual reality.'},
                {'book_id': 5, 'title': 'Pride and Prejudice', 'average_rating': 4.4, 'ratings_count': 80000, 'description': 'Classic romance and social commentary.'},
                {'book_id': 6, 'title': 'Altered Carbon', 'average_rating': 4.2, 'ratings_count': 35000, 'description': 'Gritty Sci-Fi noir where consciousness can be transferred.'},
            ])
            
        generator.build_index(books_df)
    else:
        print("Loading existing FAISS index...")
        generator.load_index()
        
    return generator

def recommend(user_profile: str, generator: CandidateGenerator, reranker: LLMReranker, session_history: list, rating_pref: float = 4.0, finished_books: list = None):
    """
    Runs the full recommendation pipeline:
    1. Parse user string into a vector.
    2. Stage 1: Get Top K from FAISS.
    3. Stage 2: Rerank Top K using Ollama.
    """
    if finished_books is None:
        finished_books = []
    
    print(f"\n======== STARTING PIPELINE ========\nUser Profile: '{user_profile}'")
    if finished_books:
        print(f"Finished books context: {len(finished_books)} items.")
    
    # Extract keywords so conversational fluff doesn't break TF-IDF
    print("\n[Stage 1] Extracting core keywords with Ollama...")
    try:
        import ollama
        
        # Build context from history and finished books
        history_context = ""
        if session_history:
            history_context += "Previous constraints:\n" + "\n".join([f"- {h}" for h in session_history]) + "\n\n"
        
        if finished_books:
            history_context += "The user just finished reading these books:\n"
            for b in finished_books:
                title = b['title']
                rating = b.get('user_rating', '?')
                pages = b.get('total_pages', '?')
                
                # Enrich with real metadata from the database
                db_info = generator.lookup_book_by_title(title)
                if db_info:
                    desc = db_info.get('description', '')[:200]
                    tags = db_info.get('tags', '')
                    db_rating = db_info.get('average_rating', '')
                    history_context += f"- '{db_info.get('title', title)}' (User Rating: {rating}/5, Pages: {pages})"
                    if tags:
                        history_context += f" [Tags: {tags}]"
                    if desc:
                        history_context += f" Description: {desc}"
                    history_context += "\n"
                    print(f"  Enriched '{title}' with DB metadata: tags={tags[:80]}...")
                else:
                    history_context += f"- '{title}' (User Rating: {rating}/5, Pages: {pages})\n"
                    print(f"  Book '{title}' not found in database, using title only.")
            history_context += "\n"
            
        extraction_prompt = f"""{history_context}Extract only the core topics, genres, or titles from this NEW user request. 
If the user asks for "something different", "else", or "not that", generate new keyword concepts that are adjacent to the previous ones but distinctly different, and EXCLUDE the old keywords.
Otherwise, just output EXACTLY what they asked for in its simplest keyword form. Do NOT invent new series or spin-offs!
Return ONLY a space-separated list of keywords. Keep titles separated by spaces appropriately.
New Request: {user_profile}"""

        res = ollama.generate(model=reranker.model_name, prompt=extraction_prompt)
        search_query = res['response'].strip().replace("'", "").replace('"', "").replace("_", " ")
        print(f"Extracted Search Keywords: '{search_query}'")
    except Exception as e:
        search_query = user_profile
        print(f"Keyword extraction failed, using raw profile: {e}")
    
    # 2. Candidate Generation (Stage 1)
    print("\n[Stage 1] Querying FAISS for Top Candidates...")
    query_vector = generator.get_book_vector(search_query, rating_pref)
    
    print("[Stage 1] Extracting Results...")
    # Adjust K dynamically, expand to 300 to get a wide variety of matches before sorting
    expanded_k = min(300, generator.index.ntotal) if generator.index is not None else 300
    candidates_df = generator.generate_candidates(query_vector, top_k=expanded_k)
    
    # Calculate a hybrid score so high-rated books rise to the top of the semantic matches
    if not candidates_df.empty and 'average_rating' in candidates_df.columns:
        # Convert to float to be safe
        candidates_df['average_rating'] = candidates_df['average_rating'].astype(float)
        
        # Exponentially punish lower semantic matches so obscure 5.0s don't beat perfect 4.0s
        # 0.89^10 = 0.31, whereas 0.99^10 = 0.90
        candidates_df['hybrid_score'] = (candidates_df['similarity_score'] ** 10) * candidates_df['average_rating']
        candidates_df = candidates_df.sort_values(by='hybrid_score', ascending=False)
        
    candidates_df = candidates_df.head(TOP_K_CANDIDATES)
    
    print(f"\n=== Stage 1 Results (Top {TOP_K_CANDIDATES} after Hybrid Sort) ===")
    print(candidates_df[['title', 'similarity_score', 'average_rating']].to_string(index=False).encode('utf-8', 'ignore').decode('utf-8'))
    
    # 3. Semantic Reranking (Stage 2)
    print("\n[Stage 2] Passing candidates to Ollama for context-aware reranking...")
    reranked_results = reranker.rerank(user_profile, candidates_df)
    
    print("\n=== Stage 2 Final Recommended Order ===")
    for i, book in enumerate(reranked_results):
        title = book.get('title', 'Unknown')
        print(f"{i+1}. {title} (ID: {book.get('id')})")
        if 'reasoning' in book:
             print(f"   Reason: {book.get('reasoning')}")
             
    return reranked_results

if __name__ == "__main__":
    print("\n================================================")
    print("  Goodreads Hybrid Recommender (FAISS + Ollama) ")
    print("================================================")
    
    # Pre-load the system so it doesn't build on every loop
    print("Initializing system...")
    try:
        generator = initialize_system()
        reranker = LLMReranker()
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
                
            recommend(profile, generator, reranker, session_history)
            
            # Keep history short to avoid confounding the LLM too far back
            session_history.append(profile)
            if len(session_history) > 3:
                session_history.pop(0)
            
        except KeyboardInterrupt:
            print("\nGoodbye!")
            break