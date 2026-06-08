#pragma once

#include <string>
#include <vector>

class EpubHandler {
public:
  EpubHandler();
  ~EpubHandler();

  // Loads an EPUB file and extracts text
  bool loadEpub(const std::string &filepath);

  // Get the title of the loaded book
  std::string getTitle() const;

  // Extract metadata directly from the file
  static bool getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out);

  // Get the content (current page text)
  std::string getContent() const;

  void nextPage();
  void prevPage();
  void jumpToPage(int page);

  int getCurrentPage() const;
  int getTotalPages() const;

  bool hasNextPage() const;
  bool hasPrevPage() const;

private:
  std::string m_filepath;
  std::string m_title;
  std::vector<std::string> m_pages;
  int m_currentPage;
  bool m_isLoaded;

  std::string readZipFile(struct zip *z, const std::string &filepath);
  std::string stripHtmlTags(const std::string &html,
                            const std::string &bookTitle);
  std::string replaceUtf8Characters(const std::string &str);
  void paginateText(const std::string &text);
};
