import sys
import io
from pathlib import Path
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List, Dict, Any, Optional
import uvicorn

# Ensure that the root directory is accessible for imports if run directly
sys.path.append(str(Path(__file__).resolve().parent.parent))

from src.main import initialize_system, recommend

app = FastAPI(
    title="Goodreads Recommender API",
    description="A pure Semantic Recommender using ChromaDB and local Langchain Embeddings.",
    version="2.0.0"
)

# Global model instances so Chroma replaces FAISS in RAM
generator = None

# --- Pydantic Data Models (Swagger) ---
class FinishedBook(BaseModel):
    title: str
    total_pages: int
    user_rating: int

class RecommendationRequest(BaseModel):
    user_profile: str
    session_history: Optional[List[str]] = []
    rating_pref: Optional[float] = 4.0
    finished_books: Optional[List[FinishedBook]] = []
    use_reviews: Optional[bool] = False
    exact_match: Optional[bool] = False
    language: Optional[str] = 'eng'

class RecommendationResponse(BaseModel):
    recommendations: List[Dict[str, Any]]
    
# --- Server Lifecycle ---
@app.on_event("startup")
def startup_event():
    """
    On server boot, load the ChromaDB vector database so queries run instantly.
    """
    global generator
    print("==================================================")
    print("  Booting Semantic Recommender Engine...          ")
    print("==================================================")
    try:
        generator = initialize_system()
        print("\n[SUCCESS] Subsystems Loaded. API Gateway is accepting connections.")
    except Exception as e:
        print(f"Failed to initialize recommender models: {e}")
        sys.exit(1)

import ssl
import urllib.request
from fastapi.responses import Response

# --- Endpoints ---
@app.get("/api/image")
def proxy_image(url: str):
    """
    Proxy endpoint to fetch external HTTPS images and serve them over local HTTP.
    This bypasses the C++ client's lack of OpenSSL/HTTPS support.
    """
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        with urllib.request.urlopen(req, timeout=10, context=ctx) as response:
            data = response.read()
            content_type = response.headers.get('Content-Type', 'image/jpeg')
            return Response(content=data, media_type=content_type)
    except Exception as e:
        print(f"Failed to proxy image {url}: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/recommend", response_model=RecommendationResponse)
def get_recommendations(req: RecommendationRequest):
    """
    Primary endpoint for the LVGL C++ Application. 
    Accepts natural language user input and returns a curated JSON array of book results.
    """
    if generator is None:
         raise HTTPException(status_code=503, detail="Models are not initialized yet.")
         
    try:
        results = recommend(
            user_profile=req.user_profile,
            generator=generator,
            session_history=req.session_history,
            rating_pref=req.rating_pref,
            finished_books=[b.model_dump() for b in req.finished_books] if req.finished_books else [],
            use_reviews=req.use_reviews,
            target_language=req.language,
            exact_match=req.exact_match
        )
        return RecommendationResponse(recommendations=results)
    except Exception as e:
        print(f"Exception during Recommendation Generation: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/health")
def health_check():
    """Simple ping for the C++ application to check if server is alive."""
    if generator is None:
        return {"status": "loading"}
    return {"status": "active"}

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
