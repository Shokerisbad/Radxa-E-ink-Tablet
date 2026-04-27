import pandas as pd
from src.dataset_loader import load_books_metadata
df = load_books_metadata()
print('Total Books:', len(df))
print('Books >= 10k ratings:', len(df[df['ratings_count'] >= 10000]))
print('Books >= 5k ratings:', len(df[df['ratings_count'] >= 5000]))
print('Books >= 1k ratings:', len(df[df['ratings_count'] >= 1000]))
