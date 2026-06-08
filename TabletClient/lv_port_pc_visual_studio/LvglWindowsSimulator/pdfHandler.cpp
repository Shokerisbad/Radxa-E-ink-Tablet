#include "pdfHandler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#undef FT2_BUILD_LIBRARY
#include <mupdf/fitz.h>

// LVGL does not bundle stb_image_write, so we implement a quick 24-bit BMP
// writer here
static void write_bmp(const char *filename, int w, int h, int comp, int stride,
                      const unsigned char *data) {
  FILE *f = fopen(filename, "wb");
  if (!f)
    return;
  int pad = (4 - ((w * 3) % 4)) % 4;
  int data_sz = (w * 3 + pad) * h;
  unsigned char header[54] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
                              40,  0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 1, 0,
                              24,  0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,
                              0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0};
  int filesize = 54 + data_sz;
  header[2] = (unsigned char)(filesize);
  header[3] = (unsigned char)(filesize >> 8);
  header[4] = (unsigned char)(filesize >> 16);
  header[5] = (unsigned char)(filesize >> 24);
  header[18] = (unsigned char)(w);
  header[19] = (unsigned char)(w >> 8);
  header[20] = (unsigned char)(w >> 16);
  header[21] = (unsigned char)(w >> 24);
  header[22] = (unsigned char)(h);
  header[23] = (unsigned char)(h >> 8);
  header[24] = (unsigned char)(h >> 16);
  header[25] = (unsigned char)(h >> 24);
  header[34] = (unsigned char)(data_sz);
  header[35] = (unsigned char)(data_sz >> 8);
  header[36] = (unsigned char)(data_sz >> 16);
  header[37] = (unsigned char)(data_sz >> 24);
  fwrite(header, 1, 54, f);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      const unsigned char *pixel = data + (y * stride) + (x * comp);
      unsigned char r = pixel[0];
      unsigned char g = pixel[1];
      unsigned char b = pixel[2];

      // Calculate luminance (standard BT.601)
      unsigned char luma =
          (unsigned char)(0.299f * r + 0.587f * g + 0.114f * b);

      fputc(luma, f);
      fputc(luma, f);
      fputc(luma, f);
    }
    for (int p = 0; p < pad; p++)
      fputc(0, f);
  }
  fclose(f);
  
  std::cout << "[PDF] Wrote BMP " << filename << " (" << w << "x" << h << ", comp=" << comp << ", stride=" << stride << ")" << std::endl;
}

PdfHandler::PdfHandler()
    : m_isLoaded(false), m_ctx(nullptr), m_doc(nullptr), m_pageCount(0),
      m_currentPage(0) {
  m_ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
  if (!m_ctx) {
    std::cerr << "Failed to create MuPDF context\n";
  } else {
    fz_register_document_handlers(m_ctx);
  }
}

PdfHandler::~PdfHandler() {
  if (m_doc) {
    fz_drop_document(m_ctx, m_doc);
  }
  if (m_ctx) {
    fz_drop_context(m_ctx);
  }
}

bool PdfHandler::loadPdf(const std::string &filepath) {
  if (!m_ctx)
    return false;
  if (!std::filesystem::exists(filepath)) {
    return false;
  }

  m_filepath = filepath;
  m_title = std::filesystem::path(filepath).filename().string();

  fz_context *ctx = m_ctx;

  fz_try(ctx) {
    m_doc = fz_open_document(ctx, filepath.c_str());
    m_pageCount = fz_count_pages(ctx, m_doc);
    m_currentPage = 0;
    m_isLoaded = true;

    // Setup output directory
    std::string cache_dir = "books/.cache/pdf_imgs/";
    if (!std::filesystem::exists(cache_dir)) {
      std::filesystem::create_directories(cache_dir);
    }
  }
  fz_catch(ctx) {
    std::cerr << "MuPDF failed to load document\n";
    return false;
  }

  if (m_isLoaded) {
    renderPageToBmp();
  }

  return true;
}

void PdfHandler::renderPageToBmp() {
  if (!m_isLoaded || !m_doc)
    return;

  fz_page *page = nullptr;
  fz_pixmap *pix = nullptr;

  fz_context *ctx = m_ctx;

  fz_try(ctx) {
    page = fz_load_page(ctx, m_doc, m_currentPage);

    fz_rect bounds = fz_bound_page(ctx, page);

    float page_w = bounds.x1 - bounds.x0;
    float page_h = bounds.y1 - bounds.y0;

    // The document must fit perfectly into the 460x680 bounds.
    float target_w = 460.0f;
    float target_h = 680.0f;

    float zoom_x = target_w / page_w;
    float zoom_y = target_h / page_h;
    float zoom = std::min(zoom_x, zoom_y);

    // Apply scale
    fz_matrix ctm = fz_scale(zoom, zoom);

    // Bounds will now represent the absolute pixel dimensions
    bounds = fz_transform_rect(bounds, ctm);

    // Translate to origin so it's not offset
    ctm = fz_concat(ctm, fz_translate(-bounds.x0, -bounds.y0));
    bounds = fz_transform_rect(bounds, fz_translate(-bounds.x0, -bounds.y0));

    pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx),
                                  fz_round_rect(bounds), NULL, 0);
    fz_clear_pixmap_with_value(ctx, pix, 0xFF);

    fz_device *dev = fz_new_draw_device(ctx, fz_identity, pix);
    fz_run_page(ctx, page, dev, ctm, NULL);
    fz_close_device(ctx, dev);
    fz_drop_device(ctx, dev);
  }
  fz_always(ctx) {
    if (page)
      fz_drop_page(ctx, page);
  }
  fz_catch(ctx) { std::cerr << "MuPDF failed to render page\n"; }

  if (pix) {
    // Write the pixmap out
    std::string out_path = "books/.cache/pdf_imgs/" + m_title + "_page_" +
                           std::to_string(m_currentPage) + ".bmp";
    write_bmp(out_path.c_str(), pix->w, pix->h, pix->n, pix->stride,
              pix->samples);
    fz_drop_pixmap(ctx, pix);
  }
}

void PdfHandler::nextPage() {
  if (m_isLoaded && m_currentPage < m_pageCount - 1) {
    m_currentPage++;
    renderPageToBmp();
  }
}

void PdfHandler::prevPage() {
  if (m_isLoaded && m_currentPage > 0) {
    m_currentPage--;
    renderPageToBmp();
  }
}

void PdfHandler::jumpToPage(int page) {
  if (!m_isLoaded || m_pageCount <= 0) return;
  if (page < 0) page = 0;
  if (page >= m_pageCount) page = m_pageCount - 1;
  if (m_currentPage != page) {
      m_currentPage = page;
      renderPageToBmp();
  }
}

int PdfHandler::getCurrentPage() const {
    return m_currentPage;
}

int PdfHandler::getTotalPages() const {
    return m_pageCount;
}

std::string PdfHandler::getTitle() const {
  if (!m_isLoaded)
    return "No PDF Loaded";
  return m_title;
}

std::string PdfHandler::getContent() const {
  if (!m_isLoaded)
    return "Please load a PDF first.";

  std::string bmp_path = "books/.cache/pdf_imgs/" + m_title + "_page_" +
                         std::to_string(m_currentPage) + ".bmp";
  return "[IMG:A:" + bmp_path + "]";
}

bool PdfHandler::getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out) {
    if (!std::filesystem::exists(filepath)) return false;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return false;

    fz_register_document_handlers(ctx);
    fz_document* doc = nullptr;
    bool success = false;

    fz_try(ctx) {
        doc = fz_open_document(ctx, filepath.c_str());
        if (doc) {
            char title_buf[256] = {0};
            char author_buf[256] = {0};
            
            if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_TITLE, title_buf, sizeof(title_buf)) > 0) {
                title_out = title_buf;
            }
            if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_AUTHOR, author_buf, sizeof(author_buf)) > 0) {
                author_out = author_buf;
            }
            success = true;
        }
    }
    fz_always(ctx) {
        if (doc) fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx) {
        success = false;
    }

    return success;
}
