#pragma once
#include <initializer_list>
#include <string>
#include <string_view>

namespace gameui {
enum class MenuLanguage { English, German, French, Italian, Spanish };
// Set once before menu startup from the host's validated effective game language.
// Unknown language names use English. Custom skin text and guest-provided names are not translated.
void setMenuLanguage(std::string_view name);
MenuLanguage menuLanguage();
const char* menuText(std::string_view english);
// Substitute {0}, {1}, ... after selecting the translation; arguments stay verbatim.
std::string menuFormat(std::string_view english, std::initializer_list<std::string_view> arguments);
} // namespace gameui
