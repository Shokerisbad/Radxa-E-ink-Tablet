#pragma once

#undef FT2_BUILD_LIBRARY

#include <string>

// Forward declarations for MuPDF to avoid pulling the heavy header into
// everything
struct fz_context;
struct fz_document;

class PdfHandler {
public:
  PdfHandler();
  ~PdfHandler();

  // Loads a PDF file and queries the page count
  bool loadPdf(const std::string &filepath);

  // Get the title of the loaded pdf
  std::string getTitle() const;

  // Extract metadata directly from the file
  static bool getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out);

  // Returns the path to the extracted BMP page as an LVGL `IMG:` marker
  std::string getContent() const;

  void nextPage();
  void prevPage();
  void jumpToPage(int page);

  int getCurrentPage() const;
  int getTotalPages() const;

private:
  void renderPageToBmp();

  std::string m_filepath;
  std::string m_title;
  bool m_isLoaded;

  fz_context *m_ctx;
  fz_document *m_doc;
  int m_pageCount;
  int m_currentPage;
};
