#include <iostream>
#include <string>
#include <stdio.h>

int main() {
    std::string url = "https://openlibrary.org/search.json?title=Warriors%20Into%20the%20Wild&author=Erin%20Hunter";
    std::string cmd = "curl -k -L -s \"" + url + "\" 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) { std::cout << "popen failed\n"; return 1; }
    char buffer[512];
    std::string response;
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        response += buffer;
    }
    pclose(fp);
    std::cout << "Response length: " << response.length() << std::endl;
    std::cout << "Snippet: " << response.substr(0, 100) << std::endl;
    return 0;
}
