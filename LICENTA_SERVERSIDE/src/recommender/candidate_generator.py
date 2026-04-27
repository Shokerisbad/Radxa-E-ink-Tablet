import pandas as pd
from typing import List, Dict
from pathlib import Path
import os
import shutil
import uuid

from langchain_chroma import Chroma
from langchain_ollama import OllamaEmbeddings
from langchain_core.documents import Document
from tqdm import tqdm

from src.dataset_loader import load_books_metadata
from src.config import DATA_DIR, TOP_K_CANDIDATES

class CandidateGenerator:
    def __init__(self, db_dir: str = str(DATA_DIR / "chroma_db")):
        self.db_dir = db_dir
        # Using the fast nomic embeddings model you previously pulled!
        self.embeddings = OllamaEmbeddings(model="nomic-embed-text")
        self.vector_store = None
        self.books_df = None
        
        if os.path.exists(self.db_dir):
            print(f"Connecting to existing ChromaDB at {self.db_dir}...")
            self.vector_store = Chroma(
                embedding_function=self.embeddings,
                persist_directory=self.db_dir
            )
            
    def _create_documents(self) -> List[Document]:
        docs = []
        # Filter dropped rows explicitly
        valid_books = self.books_df.dropna(subset=['title'])
        for _, row in tqdm(valid_books.iterrows(), total=len(valid_books), desc="Formatting Semantics"):
            # The embedded context! This is what Nomic physically "reads" and converts to a vector.
            title = str(row.get('title', ''))
            desc = str(row.get('description', ''))[:1000] # Cut off overly long essays
            tags = str(row.get('tags', ''))
            content = f"Title: {title}\nTags: {tags}\nDescription: {desc}"
            
            # The metadata returned structurally to the C++ UI
            doc = Document(
                page_content=content,
                metadata={
                    "id": str(row.get("book_id", "")),
                    "title": title,
                    "average_rating": float(row.get("average_rating", 0)),
                    "tags": tags,
                    "publication_year": str(row.get("publication_year", "")),
                    "num_pages": str(row.get("num_pages", "")),
                    "image_url": str(row.get("image_url", ""))
                }
            )
            docs.append(doc)
        return docs

    def build_index(self):
        """
        Ingests the dataset into ChromaDB.
        """
        if os.path.exists(self.db_dir):
            print("Clearing old Chroma Database...")
            if self.vector_store is not None:
                # Force python to let go of the SQLite file lock so windows allows rmtree
                self.vector_store = None
                import gc
                gc.collect()
            try:
                shutil.rmtree(self.db_dir)
            except Exception as e:
                print(f"[Warning] Could not delete folder (it might be locked). Reusing existing DB! Err: {e}")
            
        docs = self._create_documents()
        ids = [str(uuid.uuid4()) for _ in docs]
        
        batch_size = 50
        print(f"Ingesting {len(docs)} documents into ChromaDB (Batch Size: {batch_size})...")
        print("NOTE: Because this uses Semantic Embeddings geometrically mapped locally, this will take 2-3 hours!")
        
        self.vector_store = Chroma(
            embedding_function=self.embeddings,
            persist_directory=self.db_dir
        )
        
        # Add documents in batches
        for i in tqdm(range(0, len(docs), batch_size), desc="Ingesting Batches"):
            batch = docs[i:i + batch_size]
            batch_ids = ids[i:i + batch_size]
            self.vector_store.add_documents(documents=batch, ids=batch_ids)
            
        print("Chroma Vector Store built and persisted successfully!")

    def generate_candidates(self, query: str, top_k: int = TOP_K_CANDIDATES) -> pd.DataFrame:
        """
        Queries ChromaDB for the closest semantic matches to the user profile text!
        """
        if self.vector_store is None:
            print("[Error] Vector store not loaded. Run initialization build_index() script first!")
            return pd.DataFrame()
            
        # Perform distance search (L2 distance by default in Chroma)
        results = self.vector_store.similarity_search_with_score(query, k=top_k)
        
        # Convert to dataframe
        formatted = []
        for doc, score in results:
            meta = doc.metadata.copy()
            meta['similarity_score'] = float(score)  # In L2, lower is better!
            formatted.append(meta)
            
        return pd.DataFrame(formatted)
