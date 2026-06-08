#include "epubHandler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <zip.h> // libzip Header

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "../LvglPlatform/lvgl/src/libs/gltf/stb_image/stb_image.h"

// LVGL does not bundle stb_image_write, so we implement a quick 24-bit BMP
// writer here
static void write_bmp(const char *filename, int w, int h, int comp,
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
      fputc(data[(y * w + x) * comp + 2], f);
      fputc(data[(y * w + x) * comp + 1], f);
      fputc(data[(y * w + x) * comp + 0], f);
    }
    for (int p = 0; p < pad; p++)
      fputc(0, f);
  }
  fclose(f);
}

// Basic EPUB Handler stub for the LVGL Simulator
EpubHandler::EpubHandler() : m_isLoaded(false), m_currentPage(0) {}

EpubHandler::~EpubHandler() {}

std::string EpubHandler::readZipFile(zip_t *z, const std::string &path) {
  zip_file_t *zf = zip_fopen(z, path.c_str(), 0);
  if (!zf)
    return "";
  std::string content;
  char buf[1024];
  while (auto bytes_read = zip_fread(zf, buf, sizeof(buf))) {
    if (bytes_read < 0)
      break;
    content.append(buf, bytes_read);
  }
  zip_fclose(zf);
  return content;
}

std::string EpubHandler::replaceUtf8Characters(const std::string &str) {
  std::string result = str;

  // Array of pairs: {UTF-8 sequence, ASCII replacement}
  std::vector<std::pair<std::string, std::string>> replacements = {
      {"\xE2\x80\x98", "'"},   // Left single quotation mark
      {"\xE2\x80\x99", "'"},   // Right single quotation mark
      {"\xE2\x80\x9A", "'"},   // Single low-9 quotation mark
      {"\xE2\x80\x9B", "'"},   // Single high-reversed-9 quotation mark
      {"\xE2\x80\x9C", "\""},  // Left double quotation mark
      {"\xE2\x80\x9D", "\""},  // Right double quotation mark
      {"\xE2\x80\x9E", "\""},  // Double low-9 quotation mark
      {"\xE2\x80\x93", "-"},   // En dash
      {"\xE2\x80\x94", "-"},   // Em dash
      {"\xE2\x80\xA6", "..."}, // Horizontal ellipsis
      {"\xC2\xA0", " "}        // Non-breaking space
  };

  for (const auto &pair : replacements) {
    size_t pos = 0;
    while ((pos = result.find(pair.first, pos)) != std::string::npos) {
      result.replace(pos, pair.first.length(), pair.second);
      pos += pair.second.length();
    }
  }

  return result;
}

std::string EpubHandler::stripHtmlTags(const std::string &html,
                                       const std::string &bookTitle) {
  std::string result;
  bool inTag = false;
  std::string tagName;
  for (size_t i = 0; i < html.length(); ++i) {
    char c = html[i];
    if (c == '<') {
      inTag = true;
      tagName = "";
    } else if (c == '>') {
      inTag = false;
      std::string t = tagName;
      for (auto &tc : t)
        tc = tolower(tc);

      if (t.find("img") == 0) {
        size_t src_pos = t.find("src=\"");
        if (src_pos != std::string::npos) {
          size_t end_pos = t.find("\"", src_pos + 5);
          if (end_pos != std::string::npos) {
            std::string src =
                tagName.substr(src_pos + 5, end_pos - (src_pos + 5));
            std::string filename = std::filesystem::path(src)
                                       .filename()
                                       .replace_extension(".bmp")
                                       .string();
            result += "\n\n[IMG:A:books/.cache/" + bookTitle + "_imgs/" +
                      filename + "]\n\n";
          }
        }
      } else if (t == "p" || t == "/p" || t == "br" || t == "br/" ||
                 t == "br /" || t == "div" || t == "/div" || t == "h1" ||
                 t == "/h1" || t == "h2" || t == "/h2" || t == "h3" ||
                 t == "/h3") {
        result += "\n";
      } else {
        result += ' ';
      }
    } else if (inTag) {
      tagName += c;
    } else {
      if (c == '&') {
        // Decode basic HTML entities to prevent LVGL rendering boxes
        std::string entity;
        size_t j = i + 1;
        while (j < html.length() && html[j] != ';' && (j - i) < 10) {
          entity += html[j];
          j++;
        }
        if (j < html.length() && html[j] == ';') {
          if (entity == "nbsp")
            result += ' ';
          else if (entity == "lt")
            result += '<';
          else if (entity == "gt")
            result += '>';
          else if (entity == "amp")
            result += '&';
          else if (entity == "quot")
            result += '"';
          else if (entity == "apos")
            result += '\'';
          else if (entity == "#39")
            result += '\'';
          else if (entity == "mdash")
            result += '-';
          else if (entity == "ndash")
            result += '-';
          else if (entity == "lsquo")
            result += '\'';
          else if (entity == "rsquo")
            result += '\'';
          else if (entity == "ldquo")
            result += '"';
          else if (entity == "rdquo")
            result += '"';
          else {
            // If we don't recognize the entity, drop it or fallback
          }
          i = j; // skip over the entity and the semicolon
        } else {
          result += c; // wasn't a valid entity, just add the ampersand
        }
      } else {
        if (c == '\n' || c == '\r') {
          result += ' ';
        } else {
          result += c;
        }
      }
    }
  }
  return result;
}

void EpubHandler::paginateText(const std::string &text) {
  // Rough estimate matching LVGL default font (montserrat 14)
  const int SCREEN_MAX_HEIGHT = 730; // Max height for reader_body is 770, leave 40px pad for bottom page counter
  const int CHARS_PER_LINE = 65; 
  const int LINE_HEIGHT = 16;

  std::string current_page;
  int current_height = 0;

  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line, '\n')) {
    size_t img_idx = line.find("[IMG:");
    if (img_idx != std::string::npos) {
      // If there's an image, force a page break before it if we have text
      if (!current_page.empty()) {
        m_pages.push_back(current_page);
        current_page.clear();
        current_height = 0;
      }
      // Push the image on its own page
      m_pages.push_back(line + "\n");
      continue;
    }

    // Estimate wrapped lines
    size_t line_len = line.length();
    int wrapped_lines = (static_cast<int>(line_len) / CHARS_PER_LINE) + 1;
    if (line_len == 0)
      wrapped_lines = 1; // Empty lines still take up height

    int added_height = wrapped_lines * LINE_HEIGHT;

    if (current_height + added_height > SCREEN_MAX_HEIGHT &&
        !current_page.empty()) {
      m_pages.push_back(current_page);
      current_page = line + "\n";
      current_height = added_height;
    } else {
      current_page += line + "\n";
      current_height += added_height;
    }
  }

  if (!current_page.empty()) {
    m_pages.push_back(current_page);
  }

  if (m_pages.empty())
    m_pages.push_back("No content found in the epub.");
}

bool EpubHandler::loadEpub(const std::string &filepath) {
  if (!std::filesystem::exists(filepath)) {
    m_pages.push_back("File not found: " + filepath);
    return false;
  }

  m_filepath = filepath;
  m_title = std::filesystem::path(filepath).filename().string();
  m_pages.clear();
  m_currentPage = 0;

  int err = 0;
  zip_t *z = zip_open(filepath.c_str(), 0, &err);
  if (!z) {
    m_pages.push_back("Failed to open EPUB archive.");
    m_isLoaded = false;
    return false;
  }

  std::string full_text;
  zip_int64_t num_entries = zip_get_num_entries(z, 0);

  // Ensure cache directory exists for images
  std::string book_img_dir = "books/.cache/" + m_title + "_imgs/";
  std::filesystem::create_directories(book_img_dir);

  for (zip_int64_t i = 0; i < num_entries; i++) {
    const char *name = zip_get_name(z, i, 0);
    if (!name)
      continue;

    std::string sname(name);
    std::string sname_lower = sname;
    for (auto &tc : sname_lower)
      tc = tolower(tc);

    // Extract images
    if (sname_lower.find(".jpg") != std::string::npos ||
        sname_lower.find(".jpeg") != std::string::npos ||
        sname_lower.find(".png") != std::string::npos ||
        sname_lower.find(".bmp") != std::string::npos) {

      std::string filename = std::filesystem::path(sname).filename().string();
      std::string out_path = book_img_dir + filename;

      if (!std::filesystem::exists(out_path)) {
        std::string img_content = readZipFile(z, sname);
        std::ofstream out(out_path, std::ios::binary);
        out.write(img_content.data(), img_content.size());
        out.close();

        // Try to decode as progressive JPEG/PNG and rewrite to simple 24-bit
        // BMP because LVGL BMP decoder is highly robust
        int width, height, channels;
        unsigned char *img_data =
            stbi_load(out_path.c_str(), &width, &height, &channels, 3);

        if (img_data) {
          int max_w = 460;
          int max_h = 660;
          int new_w = width;
          int new_h = height;

          // Downscale proportionally if needed
          if (width > max_w || height > max_h) {
            float scale_w = (float)max_w / width;
            float scale_h = (float)max_h / height;
            float scale = std::min(scale_w, scale_h);
            new_w = (int)(width * scale);
            new_h = (int)(height * scale);
            if (new_w < 1)
              new_w = 1;
            if (new_h < 1)
              new_h = 1;
          }

          unsigned char *new_data = new unsigned char[new_w * new_h * 3];
          for (int y = 0; y < new_h; y++) {
            for (int x = 0; x < new_w; x++) {
              int src_x = x * width / new_w;
              int src_y = y * height / new_h;
              if (src_x >= width)
                src_x = width - 1;
              if (src_y >= height)
                src_y = height - 1;

              int src_idx = (src_y * width + src_x) * 3;
              int dst_idx = (y * new_w + x) * 3;
              new_data[dst_idx] = img_data[src_idx];
              new_data[dst_idx + 1] = img_data[src_idx + 1];
              new_data[dst_idx + 2] = img_data[src_idx + 2];
            }
          }

          std::string bmp_path = std::filesystem::path(out_path)
                                     .replace_extension(".bmp")
                                     .string();
          write_bmp(bmp_path.c_str(), new_w, new_h, 3, new_data);
          delete[] new_data;
          stbi_image_free(img_data);

          // Delete the original progressive JPG/PNG so we don't waste space
          std::filesystem::remove(out_path);
        }
      }
    }
    // Extract text
    else if (sname_lower.find(".html") != std::string::npos ||
             sname_lower.find(".xhtml") != std::string::npos) {
      std::string raw_html = readZipFile(z, sname);
      full_text += stripHtmlTags(raw_html, m_title) + "\n\n";
    }
  }
  zip_close(z);

  // Collapse whitespaces slightly for better reading but preserve newlines
  std::string clean_text;
  bool last_space = false;
  bool last_newline = false;

  for (char c : full_text) {
    if (c == '\n' || c == '\r') {
      if (!last_newline) {
        clean_text += '\n';
        last_newline = true;
        last_space = true; // a newline acts as a space
      }
    } else if (std::isspace((unsigned char)c)) {
      if (!last_space) {
        clean_text += ' ';
        last_space = true;
        last_newline = false;
      }
    } else {
      clean_text += c;
      last_space = false;
      last_newline = false;
    }
  }

  // Replace UTF-8 characters that LVGL's default font doesn't support
  clean_text = replaceUtf8Characters(clean_text);

  paginateText(clean_text);
  m_isLoaded = true;
  return true;
}

std::string EpubHandler::getTitle() const {
  if (!m_isLoaded)
    return "No Book Loaded";
  return m_title;
}

std::string EpubHandler::getContent() const {
  if (!m_isLoaded || m_pages.empty())
    return "Please load an EPUB first.";
  return m_pages[m_currentPage];
}

void EpubHandler::nextPage() {
  if (hasNextPage())
    m_currentPage++;
}

void EpubHandler::prevPage() {
  if (hasPrevPage())
    m_currentPage--;
}

void EpubHandler::jumpToPage(int page) {
  if (!m_isLoaded || m_pages.empty()) return;
  if (page < 0) page = 0;
  if (page >= (int)m_pages.size()) page = (int)m_pages.size() - 1;
  m_currentPage = page;
}

int EpubHandler::getCurrentPage() const {
  return m_currentPage;
}

int EpubHandler::getTotalPages() const {
  if (!m_isLoaded) return 0;
  return (int)m_pages.size();
}

bool EpubHandler::hasNextPage() const {
  return m_currentPage < (int)m_pages.size() - 1;
}

bool EpubHandler::hasPrevPage() const { return m_currentPage > 0; }

bool EpubHandler::getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out) {
  if (!std::filesystem::exists(filepath)) return false;

  int err = 0;
  zip_t *z = zip_open(filepath.c_str(), 0, &err);
  if (!z) return false;

  auto read_file_from_zip = [&](const std::string& path) -> std::string {
    zip_file_t *zf = zip_fopen(z, path.c_str(), 0);
    if (!zf) return "";
    std::string content;
    char buf[1024];
    while (auto bytes_read = zip_fread(zf, buf, sizeof(buf))) {
      if (bytes_read < 0) break;
      content.append(buf, bytes_read);
    }
    zip_fclose(zf);
    return content;
  };

  std::string container = read_file_from_zip("META-INF/container.xml");
  if (container.empty()) {
    zip_close(z);
    return false;
  }

  std::string opf_path;
  size_t root_pos = container.find("full-path=\"");
  if (root_pos != std::string::npos) {
    root_pos += 11;
    size_t end_pos = container.find("\"", root_pos);
    if (end_pos != std::string::npos) {
      opf_path = container.substr(root_pos, end_pos - root_pos);
    }
  }

  if (opf_path.empty()) {
    zip_close(z);
    return false;
  }

  std::string opf_content = read_file_from_zip(opf_path);
  zip_close(z);

  if (opf_content.empty()) return false;

  // Extract Title
  size_t title_start = opf_content.find("<dc:title");
  if (title_start != std::string::npos) {
    title_start = opf_content.find(">", title_start) + 1;
    size_t title_end = opf_content.find("</dc:title>", title_start);
    if (title_end != std::string::npos) {
      title_out = opf_content.substr(title_start, title_end - title_start);
    }
  }

  // Extract Author
  size_t creator_start = opf_content.find("<dc:creator");
  if (creator_start != std::string::npos) {
    creator_start = opf_content.find(">", creator_start) + 1;
    size_t creator_end = opf_content.find("</dc:creator>", creator_start);
    if (creator_end != std::string::npos) {
      author_out = opf_content.substr(creator_start, creator_end - creator_start);
    }
  }

  return true;
}
