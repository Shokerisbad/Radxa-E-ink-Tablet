import sys
from pathlib import Path

# Add project root to sys.path so 'src' module can be found
sys.path.append(str(Path(__file__).resolve().parent.parent))

from src.recommender.candidate_generator import CandidateGenerator

def test_query():
    print("Loading generator...")
    gen = CandidateGenerator()
    gen.load_index()
    
    session_history = ["Something similar to star wars"]
    query = "something different"
    print(f"\nHistory: {session_history}")
    print(f"Follow-up Query: '{query}'")
    
    import ollama
    history_context = "Previous constraints:\n" + "\n".join([f"- {h}" for h in session_history]) + "\n\n"
            
    extraction_prompt = f"""{history_context}Extract only the core topics, genres, or titles from this NEW user request. 
If the user asks for "something different", "else", or "not that", generate new keyword concepts that are adjacent to the previous ones but distinctly different, and EXCLUDE the old keywords.
Ignore conversational words like 'similar to', 'I want', 'books about'. 
Return ONLY a space-separated list of keywords. Keep titles separated by spaces appropriately (e.g., 'Star Wars', not 'StarWars').
New Request: {query}"""

    res = ollama.generate(model="mistral-nemo", prompt=extraction_prompt)
    search_query = res['response'].strip().replace("'", "").replace('"', "")
    print(f"\n[LLM extraction parsing previous arrays]:\nExtracted adjacant new Keywords: '{search_query}'")
    
    vec = gen.get_book_vector(search_query)
    text_part = vec[:-2]
    if text_part.sum() == 0:
        print("\nFAILURE: Text vector is completely empty (0 matches in vocab).")
    else:
        print(f"\nSUCCESS: Text vector matched keywords. Sum: {text_part.sum()}")
        
    res = gen.generate_candidates(vec, top_k=5)
    print("\nResults:")
    # Use to_string to avoid encoding issues on Windows terminal 
    print(res[['title', 'similarity_score']].to_string(index=False).encode('utf-8', 'ignore').decode('utf-8'))

if __name__ == "__main__":
    test_query()
