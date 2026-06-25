#pragma once

#include <string>
#include <vector>

struct EpubChapter {
  std::string title;
  std::string src_file;
  int page_number;
  size_t offset;
};

class EpubHandler {
public:
  EpubHandler();
  ~EpubHandler();

  // Loads an EPUB file and extracts text
  bool loadEpub(const std::string &filepath);

  // Get the title of the loaded book
  std::string getTitle() const;

  // Extract metadata directly from the file
  static bool getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out, std::string& genre_out, std::string& summary_out);

  // Get the content (current page text)
  std::string getContent() const;

  // Get Table of Contents
  const std::vector<EpubChapter>& getTableOfContents() const;

  void nextPage();
  void prevPage();
  void jumpToPage(int page);

  int getCurrentPage() const;
  int getTotalPages() const;

  bool hasNextPage() const;
  bool hasPrevPage() const;

  bool isManga() const;

  void repaginate(int chars_per_line, int line_height);

private:
  std::string m_filepath;
  std::string m_title;
  std::vector<std::string> m_pages;
  int m_currentPage;
  bool m_isLoaded;
  bool m_isManga;
  std::vector<EpubChapter> m_toc;

  std::string readZipFile(struct zip *z, const std::string &filepath);
  std::string stripHtmlTags(const std::string &html,
                            const std::string &bookTitle);
  std::string replaceUtf8Characters(const std::string &str);
  void paginateText(const std::string &text, int chars_per_line, int line_height);

  std::string m_fullText;
  std::vector<size_t> m_pageOffsets;
};
