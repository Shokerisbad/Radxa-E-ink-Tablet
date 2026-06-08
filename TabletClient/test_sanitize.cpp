#include <iostream>
#include <cstring>
int main() {
    const char* invalid_chars = "\\/:*?\"<>|";
    std::string author = "Mary Wollstonecraft Shelley";
    for (char& c : author) {
        if (strchr(invalid_chars, c)) c = '_';
    }
    std::cout << "Result: " << author << std::endl;
    return 0;
}
