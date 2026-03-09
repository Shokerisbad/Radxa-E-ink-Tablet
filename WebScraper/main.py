from playwright.sync_api import sync_playwright
from bs4 import BeautifulSoup

import random
import time


def scrape_goodreads(url):
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)

        context = browser.new_context(
            user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
        )
        page = context.new_page()

        print(f"Loading {url}...")
        time.sleep(random.uniform(2.0, 5.0)) # random timer to not look like too much of a bot
        page.goto(url, wait_until="networkidle")


        try:
            # This selector targets the "Book details" or "Description" expander
            if page.is_visible("button.SecondaryButton.SecondaryButton--small"):
                page.click("button.SecondaryButton.SecondaryButton--small")
        except:
            pass

        soup = BeautifulSoup(page.content(), 'html.parser')

        # --- DATA EXTRACTION ---

        # RATING: No data-testId here
        rating_el = soup.find("div", class_="RatingStatistics__rating")
        rating = rating_el.text.strip() if rating_el else "N/A"

        # SUMMARY: Looking for the description container
        summary_el = soup.find("div", {"data-testid": "description"})
        # Clean up the text
        summary = summary_el.get_text(separator=" ").strip() if summary_el else "N/A"

        # GENRES:
        # We look for links that lead to /genres/
        genre_links = soup.find_all("a", href=lambda x: x and "/genres/" in x)
        # Use a set to remove duplicates, then convert back to list
        genres = list(set([g.text.strip() for g in genre_links if g.text.strip()]))

        print("\n--- RESULTS ---")
        print(f"Rating: {rating}")
        print(f"Genres: {', '.join(genres[:5])}")  # Show first 5
        print(f"Summary (Snippet): {summary}")

        browser.close()


scrape_goodreads("https://www.goodreads.com/book/show/1.Harry_Potter_and_the_Half_Blood_Prince")