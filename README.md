# Radxa E-ink Tablet

This repository contains the software ecosystem for a custom Radxa E-ink Tablet. The project is split into several interconnected components, separating the hardware-specific C++ client from the visual simulator and the Python-based recommendation backend.

## Project Structure

* **`TabletClient/`**
  The C++ application designed to run on the Radxa tablet. It manages E-ink screen rendering, touch input, and the core application logic.
  
* **`WindowsSimulator/`**
  A Visual Studio project that allows you to run and debug the tablet's user interface entirely on Windows without needing the physical Radxa hardware. It uses the exact same shared UI codebase as the tablet.
  
* **`LICENTA_SERVERSIDE/`**
  The Python backend that powers the semantic book recommendation engine. It manages heavy database operations (FAISS, SQLite FTS5) and LLM queries.

* **`WebDashboard/`**
  A web-based interface for managing the system remotely.

---

## Data Pipeline Walkthrough (LICENTA_SERVERSIDE)

Because the datasets and databases used by the recommendation engine are enormous (upwards of **20 GB**), the `LICENTA_SERVERSIDE/data/` folder is strictly excluded from version control. 

However, the repository includes a fully automated pipeline to download and rebuild the exact same data from scratch!

### How to reproduce the Data Folder

1. **Navigate to the server directory**
   ```bash
   cd LICENTA_SERVERSIDE
   ```

2. **Download the raw datasets**
   Run the download script. This will pull the heavy `.json.gz` and `.csv` files straight from the official UCSD Goodreads dataset repository into your `data/` folder.
   ```bash
   python src/download_dataset.py
   ```

3. **Build the databases**
   Once the raw files are downloaded, simply start the main server script:
   ```bash
   python src/main.py
   ```
   The script will automatically detect that this is a first-time run. It will kick off the pipeline to parse the JSONs, convert them to Parquet files, generate the ChromaDB vector index, and build the SQLite FTS5 database. 
   
   *(Note: This processing can take some time due to the massive dataset size, but it is completely hands-off!)*
