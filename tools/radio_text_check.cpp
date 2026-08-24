#include "radio_text.h"

#include <cassert>
#include <string>

int main() {
    assert(RadioVisualText("KEXP 90.3 FM") == "KEXP 90.3 FM");
    assert(RadioVisualText("Радио Москва") == "Радио Москва");
    assert(RadioVisualText("中国广播") == "中国广播");
    assert(RadioVisualText("שלום 24") == "24 םולש");
    assert(RadioVisualText("سلام") == "\xef\xbb\xa1\xef\xbb\xbc\xef\xba\xb3");
    const std::string arabic = RadioVisualText("العربية");
    assert(arabic != "العربية" && arabic.find("\xef") != std::string::npos);
    return 0;
}
