#include <gtest/gtest.h>
#include "server.hpp"

using namespace inferno;

// ─── Constructeur ─────────────────────────────────────────────────────────────

TEST(ServerTest, DefaultConstructor) {
    Server s;
    EXPECT_FALSE(s.isRunning());
    EXPECT_EQ(s.getPort(), 0);
}

TEST(ServerTest, ConstructorWithPort) {
    Server s(4243);
    EXPECT_FALSE(s.isRunning());
    EXPECT_EQ(s.getPort(), 4243);
}

// ─── start() ─────────────────────────────────────────────────────────────────

TEST(ServerTest, StartSuccess) {
    Server s(4244);
    EXPECT_TRUE(s.start());
    EXPECT_TRUE(s.isRunning());
}

TEST(ServerTest, StartTwiceFails) {
    Server s(4245);
    EXPECT_TRUE(s.start());
    // Démarrer une deuxième fois sur le même port doit échouer
    Server s2(4245);
    EXPECT_FALSE(s2.start());
}