# 📚 Radxa E-ink Tablet & Semantic Book Recommender

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Python](https://img.shields.io/badge/Python-3.9+-yellow.svg)
![LVGL](https://img.shields.io/badge/LVGL-UI_Library-green.svg)
![Architecture](https://img.shields.io/badge/Architecture-ARM64%20%7C%20Windows-lightgrey.svg)

This repository contains the complete software ecosystem for a custom **Radxa E-ink Tablet**, developed as a thesis project. It blends low-level hardware control for E-paper displays with a high-performance, AI-driven backend for semantic book recommendations.

The project is structured into modular components, isolating the embedded C++ hardware client from the visual simulator and the Python-powered recommendation engine.

---

## 🌟 Key Features

* **Custom E-ink Driver:** Optimized SPI driver for Radxa hardware with partial and full refresh handling to mitigate E-ink ghosting.
* **Semantic Book Recommender:** A powerful recommendation engine utilizing **ChromaDB** for vector similarity and an **LLM Reranker** for nuanced, contextual book suggestions.
* **Hybrid Search Engine:** Combines exact BM25 keyword matching (SQLite FTS5) with deep semantic search.
* **Cross-Platform UI:** Built with **LVGL**, the user interface is completely decoupled from the hardware, allowing full simulation and debugging on Windows.
* **Remote Web Dashboard:** A Flask-based control panel to monitor tablet telemetry (battery, CPU temp), manage e-books, and synchronize metadata over the network.

---

## 🏗️ System Architecture

The ecosystem relies on a distributed architecture:

1. **`TabletClient/` (Embedded C++)**
   * **Role:** The core application running on the physical Radxa Zero 3W (or similar) hardware.
   * **Tech Stack:** C++, LVGL, libgpiod, SPI/I2C interfaces.
   * **Details:** Directly interfaces with the GT911 touch controller and the Waveshare E-Paper display. It renders the UI and communicates with the Python backend via REST APIs.

2. **`LICENTA_SERVERSIDE/` (AI & Search Backend)**
   * **Role:** The brain of the operation, handling natural language queries and returning curated book recommendations.
   * **Tech Stack:** Python, FastAPI, ChromaDB, SQLite FTS5, Pandas, Langchain/Ollama.
   * **Details:** Processes user reading preferences, computes Bayesian averages for ratings, and applies LLM-based reranking to prioritize highly relevant semantic matches.

3. **`WebDashboard/` (Device Management)**
   * **Role:** A lightweight web interface hosted on the tablet.
   * **Tech Stack:** Python, Flask.
   * **Details:** Listens on port `8080` to provide a drag-and-drop interface for EPUB/PDF uploads, system telemetry monitoring, and metadata synchronization with Google Books/OpenLibrary APIs.

4. **`WindowsSimulator/` (Development Environment)**
   * **Role:** A Visual Studio project for rapid UI prototyping.
   * **Tech Stack:** C++, Visual Studio, LVGL Windows Port.
   * **Details:** Compiles the exact same UI codebase as the `TabletClient` into a native Windows executable, completely bypassing the need to deploy to physical hardware for every UI tweak.

---

## 🚀 Data Pipeline (Recommendation Engine)

Because the datasets and databases used by the recommendation engine are enormous (upwards of **20 GB**), the `LICENTA_SERVERSIDE/data/` folder is strictly excluded from version control. 

However, the repository includes a fully automated pipeline to download and rebuild the exact same data from scratch.

### Rebuilding the Data Folder

1. **Navigate to the server directory:**
   ```bash
   cd LICENTA_SERVERSIDE
   ```

2. **Download the raw datasets:**
   This script pulls the heavy `.json.gz` and `.csv` files straight from the official UCSD Goodreads dataset repository into your `data/` folder.
   ```bash
   python src/download_dataset.py
   ```

3. **Build the Vector and Relational Databases:**
   Once the raw files are downloaded, start the main server script:
   ```bash
   python src/main.py
   ```
   *The script will automatically detect that this is a first-time run. It will parse the JSONs, convert them to optimized Parquet files, generate the ChromaDB vector index, and build the SQLite FTS5 database.*
---

## 🛠️ Hardware Requirements

If deploying to the physical tablet:
- Radxa Zero 3W (or similar ARM64 SBC)
- Gooddisplay E-Paper Display 7'5'' (SPI) 
- GT911 Capacitive Touch Screen (I2C) preferrably from the eink display
- Linux Kernel with spidev, i2c-dev, and libgpiod support.
