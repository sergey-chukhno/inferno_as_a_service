#pragma once

#include <string>
#include <vector>
#include <utility>

namespace inferno {

class Analysis {
public:
    // This is a static utility class
    Analysis() = delete;

    // Core regex extraction methods
    static std::vector<std::string> extractEmails(const std::string& text);
    static std::vector<std::string> extractPhones(const std::string& text);
    static std::vector<std::string> extractCreditCards(const std::string& text);
    
    // Returns pair of <password_candidate, context_label>
    static std::vector<std::pair<std::string, std::string>> extractPasswords(const std::string& text);

    // Luhn algorithm verification helper
    static bool validateLuhn(const std::string& card_number);
};

} // namespace inferno
