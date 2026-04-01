#include <gtest/gtest.h>
#include "Socket.hpp"

using namespace inferno;

// ─── Constructeur par défaut ──────────────────────────────────────────────────

TEST(SocketTest, DefaultConstructor) {
    Socket s;
    EXPECT_EQ(s.getFd(), -1);
    EXPECT_FALSE(s.isValid());
    EXPECT_EQ(s.getIp(), "");
    EXPECT_EQ(s.getPort(), 0);
}

// ─── bindNode ────────────────────────────────────────────────────────────────

TEST(SocketTest, BindSuccess) {
    Socket s;
    EXPECT_TRUE(s.bindNode("127.0.0.1", 5001));
    EXPECT_TRUE(s.isValid());
    EXPECT_EQ(s.getIp(), "127.0.0.1");
    EXPECT_EQ(s.getPort(), 5001);
}

TEST(SocketTest, AcceptWithoutListenFails) {
    Socket s;
    s.bindNode("127.0.0.1", 5002);
    // Pas de listen() → acceptNode() sur un socket invalide doit retourner nullopt
    Socket s2;
    auto result = s2.acceptNode();
    EXPECT_FALSE(result.has_value());
}

// ─── listen ──────────────────────────────────────────────────────────────────

TEST(SocketTest, ListenWithoutBindFails) {
    Socket s;
    // Pas de bind → listen doit échouer
    EXPECT_FALSE(s.listen());
}

TEST(SocketTest, ListenAfterBindSuccess) {
    Socket s;
    EXPECT_TRUE(s.bindNode("127.0.0.1", 5003));
    EXPECT_TRUE(s.listen());
}

// ─── Move Constructor ─────────────────────────────────────────────────────────

TEST(SocketTest, MoveConstructorInvalidatesDonor) {
    Socket s1;
    s1.bindNode("127.0.0.1", 5004);
    int original_fd = s1.getFd();

    Socket s2(std::move(s1));

    // s2 a récupéré le fd
    EXPECT_EQ(s2.getFd(), original_fd);
    EXPECT_TRUE(s2.isValid());

    // s1 a été vidé
    EXPECT_EQ(s1.getFd(), -1);
    EXPECT_FALSE(s1.isValid());
}

// ─── Move Assignment ──────────────────────────────────────────────────────────

TEST(SocketTest, MoveAssignmentInvalidatesDonor) {
    Socket s1;
    s1.bindNode("127.0.0.1", 5005);
    int original_fd = s1.getFd();

    Socket s2;
    s2 = std::move(s1);

    EXPECT_EQ(s2.getFd(), original_fd);
    EXPECT_TRUE(s2.isValid());

    EXPECT_EQ(s1.getFd(), -1);
    EXPECT_FALSE(s1.isValid());
}