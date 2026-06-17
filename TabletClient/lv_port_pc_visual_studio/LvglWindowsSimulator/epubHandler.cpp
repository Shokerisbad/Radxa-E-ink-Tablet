#include "epubHandler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
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
EpubHandler::EpubHandler() : m_isLoaded(false), m_currentPage(0), m_isManga(false) {}

EpubHandler::~EpubHandler() {}

std::string EpubHandler::readZipFile(zip_t *z, const std::string &path) {
  zip_file_t *zf = zip_fopen(z, path.c_str(), 0);
  if (!zf)
    return "";
  std::string content;
  char buf[65536];
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

static std::string url_decode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int v = 0;
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &v);
            ret += static_cast<char>(v);
            i += 2;
        } else if (str[i] == '+') {
            ret += ' ';
        } else {
            ret += str[i];
        }
    }
    return ret;
}

std::string EpubHandler::stripHtmlTags(const std::string &html,
                                       const std::string &bookTitle) {
  std::string result;
  bool inTag = false;
  bool skipContent = false;
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

      std::string mainTag = t;
      size_t space_pos = t.find_first_of(" \t\n\r");
      if (space_pos != std::string::npos) mainTag = t.substr(0, space_pos);
      if (!mainTag.empty() && mainTag.back() == '/') mainTag.pop_back();

      if (mainTag == "head" || mainTag == "title" || mainTag == "style" || mainTag == "script") {
          skipContent = true;
      } else if (mainTag == "/head" || mainTag == "/title" || mainTag == "/style" || mainTag == "/script") {
          skipContent = false;
      }

      if (mainTag == "img" || mainTag == "image") {
        size_t start_quote = std::string::npos;
        size_t src_pos = t.find("src=\"");
        if (src_pos != std::string::npos) start_quote = src_pos + 5;
        else {
            size_t href_pos = t.find("href=\"");
            if (href_pos != std::string::npos) start_quote = href_pos + 6;
        }

        if (start_quote != std::string::npos) {
          size_t end_pos = t.find("\"", start_quote);
          if (end_pos != std::string::npos) {
            std::string src = tagName.substr(start_quote, end_pos - start_quote);
            src = url_decode(src);
            std::string filename = std::filesystem::path(src)
                                       .filename()
                                       .replace_extension(".bmp")
                                       .string();
            result += "\n\n[IMG:A:books/.cache/" + bookTitle + "_imgs/" +
                      filename + "]\n\n";
          }
        }
      } else if (mainTag == "p" || mainTag == "/p" || mainTag == "br" ||
                 mainTag == "div" || mainTag == "/div" || mainTag == "h1" ||
                 mainTag == "/h1" || mainTag == "h2" || mainTag == "/h2" || mainTag == "h3" ||
                 mainTag == "/h3") {
        result += "\n";
      } else {
        result += ' ';
      }
    } else if (inTag) {
      tagName += c;
    } else if (!skipContent) {
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


void EpubHandler::paginateText(const std::string &text, int chars_per_line, int line_height) {
  // The reader_body is 730px tall, starting at y=30.
  // We need a dynamic safety margin that grows with font size because larger
  // fonts have more variance between estimated and actual LVGL line wrapping.
  // At 14pt (line_height=16) the margin is ~10px, at 26pt (line_height=32) it's ~42px.
  const int SAFETY_MARGIN = 10 + line_height;
  const int SCREEN_MAX_HEIGHT = 730 - SAFETY_MARGIN;
  
  m_pages.clear();
  m_pageOffsets.clear();

  std::string current_page;
  int current_height = 0;

  std::istringstream stream(text);
  std::string line;
  
  size_t current_char_index = 0;
  int current_page_idx = 0;
  
  for (auto& ch : m_toc) ch.page_number = -1;

  // Helper: count Unicode characters instead of bytes for more accurate wrapping
  auto utf8_char_count = [](const std::string& s) -> int {
      int count = 0;
      for (size_t i = 0; i < s.length(); ) {
          unsigned char c = (unsigned char)s[i];
          if (c < 0x80) i += 1;
          else if ((c & 0xE0) == 0xC0) i += 2;
          else if ((c & 0xF0) == 0xE0) i += 3;
          else if ((c & 0xF8) == 0xF0) i += 4;
          else i += 1; // invalid byte, skip
          count++;
      }
      return count;
  };

  while (std::getline(stream, line, '\n')) {
    // Check TOC and force page break for new chapter
    bool chapter_start = false;
    for (auto& ch : m_toc) {
      if (ch.page_number == -1 && current_char_index >= ch.offset) {
        chapter_start = true;
        break; // Wait to assign page_number until we potentially page break
      }
    }

    if (chapter_start && !current_page.empty()) {
        m_pages.push_back(current_page);
        current_page_idx++;
        current_page.clear();
        current_height = 0;
        m_pageOffsets.push_back(current_char_index);
    }
    
    // Now assign the correct page number
    for (auto& ch : m_toc) {
      if (ch.page_number == -1 && current_char_index >= ch.offset) {
        ch.page_number = current_page_idx;
      }
    }

    size_t img_idx = line.find("[IMG:");
    if (img_idx != std::string::npos) {
      // If there's an image, force a page break before it if we have text
      if (!current_page.empty()) {
        m_pages.push_back(current_page);
        current_page_idx++;
        current_page.clear();
        current_height = 0;
        m_pageOffsets.push_back(current_char_index);
      }
      // Push the image on its own page
      m_pages.push_back(line + "\n");
      current_page_idx++;
      current_char_index += line.length() + 1;
      if (current_char_index < text.length()) m_pageOffsets.push_back(current_char_index);
      continue;
    }

    // Estimate wrapped lines using Unicode character count (not bytes)
    // for better accuracy with UTF-8 text and proportional fonts
    int char_count = utf8_char_count(line);
    int wrapped_lines = (char_count / chars_per_line) + 1;
    if (char_count == 0)
      wrapped_lines = 1; // Empty lines still take up height

    int added_height = wrapped_lines * line_height;

    if (current_height + added_height > SCREEN_MAX_HEIGHT &&
        !current_page.empty()) {
      m_pages.push_back(current_page);
      current_page_idx++;
      current_page = line + "\n";
      current_height = added_height;
      m_pageOffsets.push_back(current_char_index);
    } else {
      current_page += line + "\n";
      current_height += added_height;
    }
    
    current_char_index += line.length() + 1;
  }

  if (!current_page.empty()) {
    m_pages.push_back(current_page);
    current_page_idx++;
  }

  if (m_pages.empty())
    m_pages.push_back("No content found in the epub.");
    
  // final check for TOC
  for (auto& ch : m_toc) {
    if (ch.page_number == -1) ch.page_number = current_page_idx > 0 ? current_page_idx - 1 : 0;
  }
}

const std::vector<EpubChapter>& EpubHandler::getTableOfContents() const {
  return m_toc;
}

bool EpubHandler::loadEpub(const std::string &filepath) {
  if (!std::filesystem::exists(filepath)) {
    m_pages.push_back("File not found: " + filepath);
    return false;
  }

  m_filepath = filepath;
  m_title = std::filesystem::path(filepath).filename().string();
  m_pages.clear();
  m_toc.clear();
  m_currentPage = 0;

  int err = 0;
  zip_t *z = zip_open(filepath.c_str(), 0, &err);
  if (!z) {
    m_pages.push_back("Failed to open EPUB archive.");
    m_isLoaded = false;
    return false;
  }

  zip_int64_t num_entries = zip_get_num_entries(z, 0);

  // Ensure cache directory exists for images
  std::string book_img_dir = "books/.cache/" + m_title + "_imgs/";
  std::filesystem::create_directories(book_img_dir);

  int total_img_count = 0;

  // First pass: extract images
  for (zip_int64_t i = 0; i < num_entries; i++) {
    const char *name = zip_get_name(z, i, 0);
    if (!name) continue;

    std::string sname(name);
    std::string sname_lower = sname;
    for (auto &tc : sname_lower) tc = tolower(tc);

    // Extract images
    if (sname_lower.find(".jpg") != std::string::npos ||
        sname_lower.find(".jpeg") != std::string::npos ||
        sname_lower.find(".png") != std::string::npos ||
        sname_lower.find(".bmp") != std::string::npos) {

      total_img_count++;

      std::string filename = std::filesystem::path(sname).filename().string();
      std::string out_path = book_img_dir + filename;
      std::string bmp_path = std::filesystem::path(out_path).replace_extension(".bmp").string();

      if (!std::filesystem::exists(bmp_path)) {
        std::string img_content = readZipFile(z, sname);
        std::ofstream out(out_path, std::ios::binary);
        out.write(img_content.data(), img_content.size());
        out.close();

        // Decode as progressive JPEG/PNG and rewrite to simple 24-bit BMP
        int width, height, channels;
        unsigned char *img_data =
            stbi_load(out_path.c_str(), &width, &height, &channels, 3);

        if (img_data) {
          int max_w = 460;
          int max_h = 660;
          int new_w = width;
          int new_h = height;

          if (width > max_w || height > max_h) {
            float scale_w = (float)max_w / width;
            float scale_h = (float)max_h / height;
            float scale = std::min(scale_w, scale_h);
            new_w = (int)(width * scale);
            new_h = (int)(height * scale);
            if (new_w < 1) new_w = 1;
            if (new_h < 1) new_h = 1;
          }

          unsigned char *new_data = new unsigned char[new_w * new_h * 3];
          for (int y = 0; y < new_h; y++) {
            for (int x = 0; x < new_w; x++) {
              int src_x = x * width / new_w;
              int src_y = y * height / new_h;
              if (src_x >= width) src_x = width - 1;
              if (src_y >= height) src_y = height - 1;

              int src_idx = (src_y * width + src_x) * 3;
              int dst_idx = (y * new_w + x) * 3;
              new_data[dst_idx] = img_data[src_idx];
              new_data[dst_idx + 1] = img_data[src_idx + 1];
              new_data[dst_idx + 2] = img_data[src_idx + 2];
            }
          }

          write_bmp(bmp_path.c_str(), new_w, new_h, 3, new_data);
          delete[] new_data;
          stbi_image_free(img_data);
          std::filesystem::remove(out_path);
        }
      }
    }
  }

  // Second pass: Parse EPUB structure
  std::string container = readZipFile(z, "META-INF/container.xml");
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
    m_pages.push_back("Invalid EPUB: No OPF found.");
    return false;
  }

  std::string opf_content = readZipFile(z, opf_path);
  std::string opf_dir = std::filesystem::path(opf_path).parent_path().string();
  if (!opf_dir.empty() && opf_dir.back() != '/') opf_dir += "/";
  if (opf_dir == "\"") opf_dir = "";

  // Parse manifest
  std::map<std::string, std::string> manifest;
  size_t manifest_start = opf_content.find("<manifest");
  size_t manifest_end = opf_content.find("</manifest>");
  if (manifest_start != std::string::npos && manifest_end != std::string::npos) {
      size_t pos = manifest_start;
      while ((pos = opf_content.find("<item ", pos)) != std::string::npos && pos < manifest_end) {
          size_t id_pos = opf_content.find("id=\"");
          // Ensure we are finding id and href within this tag
          size_t tag_end = opf_content.find(">", pos);
          id_pos = opf_content.find("id=\"", pos);
          size_t href_pos = opf_content.find("href=\"", pos);
          if (id_pos != std::string::npos && id_pos < tag_end && href_pos != std::string::npos && href_pos < tag_end) {
              id_pos += 4;
              size_t id_end = opf_content.find("\"", id_pos);
              href_pos += 6;
              size_t href_end = opf_content.find("\"", href_pos);
              std::string id = opf_content.substr(id_pos, id_end - id_pos);
              std::string href = opf_content.substr(href_pos, href_end - href_pos);
              manifest[id] = href;
          }
          pos = tag_end;
      }
  }

  // Parse spine
  std::vector<std::string> spine;
  std::string ncx_id;
  size_t spine_start = opf_content.find("<spine");
  if (spine_start != std::string::npos) {
      size_t toc_attr = opf_content.find("toc=\"", spine_start);
      size_t spine_tag_end = opf_content.find(">", spine_start);
      if (toc_attr != std::string::npos && toc_attr < spine_tag_end) {
          size_t toc_end = opf_content.find("\"", toc_attr + 5);
          ncx_id = opf_content.substr(toc_attr + 5, toc_end - (toc_attr + 5));
      }

      size_t spine_end = opf_content.find("</spine>");
      size_t pos = spine_start;
      while ((pos = opf_content.find("<itemref ", pos)) != std::string::npos && pos < spine_end) {
          size_t idref_pos = opf_content.find("idref=\"", pos);
          if (idref_pos != std::string::npos) {
              idref_pos += 7;
              size_t idref_end = opf_content.find("\"", idref_pos);
              spine.push_back(opf_content.substr(idref_pos, idref_end - idref_pos));
          }
          pos += 8;
      }
  }

  // Parse NCX
  std::string ncx_path;
  if (!ncx_id.empty() && manifest.count(ncx_id)) {
      ncx_path = opf_dir + manifest[ncx_id];
  } else {
      for (zip_int64_t i = 0; i < num_entries; i++) {
          const char *name = zip_get_name(z, i, 0);
          if (name && std::string(name).find(".ncx") != std::string::npos) {
              ncx_path = name; break;
          }
      }
  }

  if (!ncx_path.empty()) {
      std::string ncx_content = readZipFile(z, ncx_path);
      size_t pos = 0;
      while ((pos = ncx_content.find("<navPoint", pos)) != std::string::npos) {
          size_t text_start = ncx_content.find("<text>", pos);
          std::string title;
          if (text_start != std::string::npos) {
              text_start += 6;
              size_t text_end = ncx_content.find("</text>", text_start);
              title = ncx_content.substr(text_start, text_end - text_start);
          }
          size_t src_start = ncx_content.find("src=\"", pos);
          std::string src;
          if (src_start != std::string::npos) {
              src_start += 5;
              size_t src_end = ncx_content.find("\"", src_start);
              src = ncx_content.substr(src_start, src_end - src_start);
              size_t hash_pos = src.find("#");
              if (hash_pos != std::string::npos) src = src.substr(0, hash_pos);
          }
          if (!title.empty() && !src.empty()) {
              EpubChapter ch;
              ch.title = title;
              std::string ncx_dir = std::filesystem::path(ncx_path).parent_path().string();
              if (!ncx_dir.empty() && ncx_dir.back() != '/') ncx_dir += "/";
              if (ncx_dir == "\"") ncx_dir = "";
              ch.src_file = ncx_dir + src;
              ch.offset = 0;
              ch.page_number = -1;
              m_toc.push_back(ch);
          }
          pos += 9;
      }
  }

  auto clean_string = [&](const std::string& input) {
      std::string clean_text;
      bool last_space = false;
      bool last_newline = false;
      for (char c : input) {
        if (c == '\n' || c == '\r') {
          if (!last_newline) { clean_text += '\n'; last_newline = true; last_space = true; }
        } else if (std::isspace((unsigned char)c)) {
          if (!last_space) { clean_text += ' '; last_space = true; last_newline = false; }
        } else {
          clean_text += c; last_space = false; last_newline = false;
        }
      }
      return replaceUtf8Characters(clean_text);
  };

  std::string full_text;
  std::map<std::string, size_t> file_offsets;

  for (const auto& id : spine) {
      if (manifest.count(id) == 0) continue;
      std::string href = manifest[id];
      std::string full_href = opf_dir + href;
      
      std::string raw_html = readZipFile(z, full_href);
      if (raw_html.empty()) continue; // skip missing files

      file_offsets[full_href] = full_text.length();
      
      std::string stripped = stripHtmlTags(raw_html, m_title);
      std::string cleaned = clean_string(stripped) + "\n\n";
      full_text += cleaned;
  }
  
  zip_close(z);

  // Manga detection: If the epub has many images and very little text,
  // we assume it's a manga/comic and strip out all text to prevent useless title pages.
  if (total_img_count > 5 && full_text.length() < total_img_count * 200) {
      m_isManga = true;
      std::string only_images;
      std::map<size_t, size_t> old_to_new;
      size_t p = 0;
      while ((p = full_text.find("[IMG:", p)) != std::string::npos) {
          size_t end_p = full_text.find(".bmp]", p);
          if (end_p != std::string::npos) {
              end_p += 4; // Point exactly to ']'
              old_to_new[p] = only_images.length();
              only_images += full_text.substr(p, end_p - p + 1) + "\n";
              p = end_p + 1;
          } else {
              p += 5;
          }
      }
      
      // Update file offsets based on the closest old offset
      for (auto& pair : file_offsets) {
          size_t old_offset = pair.second;
          size_t new_offset = 0;
          for (auto& m : old_to_new) {
              if (m.first <= old_offset) new_offset = m.second;
          }
          pair.second = new_offset;
      }
      
      full_text = only_images;
  }

  // Map TOC offsets
  for (auto& ch : m_toc) {
      if (file_offsets.count(ch.src_file)) {
          ch.offset = file_offsets[ch.src_file];
      } else {
          ch.offset = 0; // fallback if not found
      }
  }

  m_fullText = full_text;
  
  // Default to 14 pt font values if loaded directly
  paginateText(m_fullText, 65, 16);
  m_isLoaded = true;
  return true;
}

void EpubHandler::repaginate(int chars_per_line, int line_height) {
    if (!m_isLoaded || m_pages.empty() || m_pageOffsets.empty()) return;
    
    // Find the absolute character offset of the currently viewed page
    size_t current_offset = 0;
    if (m_currentPage < m_pageOffsets.size()) {
        current_offset = m_pageOffsets[m_currentPage];
    }
    
    // Re-run pagination with new parameters
    paginateText(m_fullText, chars_per_line, line_height);
    
    // Find which new page contains our old offset
    m_currentPage = 0;
    for (size_t i = 0; i < m_pageOffsets.size(); i++) {
        if (m_pageOffsets[i] > current_offset) {
            break;
        }
        m_currentPage = i;
    }
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

bool EpubHandler::isManga() const { return m_isManga; }

bool EpubHandler::getMetadata(const std::string& filepath, std::string& title_out, std::string& author_out, std::string& genre_out, std::string& summary_out) {
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

  // Extract Genre
  size_t subject_start = opf_content.find("<dc:subject");
  if (subject_start != std::string::npos) {
    subject_start = opf_content.find(">", subject_start) + 1;
    size_t subject_end = opf_content.find("</dc:subject>", subject_start);
    if (subject_end != std::string::npos) {
      genre_out = opf_content.substr(subject_start, subject_end - subject_start);
    }
  }

  // Extract Summary
  size_t desc_start = opf_content.find("<dc:description");
  if (desc_start != std::string::npos) {
    desc_start = opf_content.find(">", desc_start) + 1;
    size_t desc_end = opf_content.find("</dc:description>", desc_start);
    if (desc_end != std::string::npos) {
      summary_out = opf_content.substr(desc_start, desc_end - desc_start);
    }
  }

  return true;
}
