import os
import sys
import requests
from tqdm import tqdm
from pathlib import Path

# Add project root to sys.path so 'src' module can be found
sys.path.append(str(Path(__file__).resolve().parent.parent))

from src.config import DATA_DIR, BOOKS_DATA_PATH, INTERACTIONS_DATA_PATH

def download_file(url: str, local_path: Path):
    """
    Downloads a file with a progress bar.
    """
    if local_path.exists():
        print(f"File {local_path.name} already exists. Skipping download.")
        return

    print(f"Downloading {local_path.name} from:")
    print(f"URL: {url}")
    
    try:
        # Stream the request
        with requests.get(url, stream=True) as r:
            r.raise_for_status()
            
            # Get the total file size from headers
            total_size_in_bytes = int(r.headers.get('content-length', 0))
            block_size = 8192 # 8 Kibibytes
            
            # Use tqdm for the progress bar
            progress_bar = tqdm(total=total_size_in_bytes, unit='iB', unit_scale=True, desc=local_path.name)
            
            with open(local_path, 'wb') as f:
                for chunk in r.iter_content(chunk_size=block_size):
                    progress_bar.update(len(chunk))
                    f.write(chunk)
                    
            progress_bar.close()
            
            if total_size_in_bytes != 0 and progress_bar.n != total_size_in_bytes:
                print("ERROR: Something went wrong during the download.")
            else:
                print(f"Successfully downloaded {local_path.name}!\n")
                
    except Exception as e:
        print(f"Failed to download {local_path.name}. Error: {e}")
        # Clean up partial file
        if local_path.exists():
            os.remove(local_path)

def main():
    print(f"Starting dataset download to: {DATA_DIR}\n")
    
    # Ensure data directory exists
    os.makedirs(DATA_DIR, exist_ok=True)
    
    # Using the more stable datarepo mirror for the Datasets
    base_url = "https://datarepo.eng.ucsd.edu/mcauley_group/gdrive/goodreads/"
    
    files_to_download = [
        {
            "filename": "goodreads_books.json.gz",
            "url": base_url + "goodreads_books.json.gz",
            "path": BOOKS_DATA_PATH
        },
        {
            "filename": "goodreads_interactions.csv",
            "url": base_url + "goodreads_interactions.csv",
            "path": INTERACTIONS_DATA_PATH
        }
    ]
    
    for item in files_to_download:
        download_file(item["url"], item["path"])

    print("All downloads complete! You can now run the pipeline.")

if __name__ == "__main__":
    main()
