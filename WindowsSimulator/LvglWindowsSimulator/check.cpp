#include <fstream>
#include <iostream>
#include <string>


int main() {
  std::string path = "books/.cache/pdf_imgs/Alex Huneycutt's Curriculum for "
                     "the Solo Artist (RadioRunner).pdf_page_0.bmp";
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cout << "file not found\n";
    return 1;
  }
  f.seekg(18);
  int32_t w = 0, h = 0;
  f.read((char *)&w, 4);
  f.read((char *)&h, 4);
  std::cout << "Generated BMP dimensions: " << w << "x" << h << "\n";
  return 0;
}
