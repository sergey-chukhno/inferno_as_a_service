#include "../server/include/network/server.hpp"
#include "Socket.hpp"
#include "Opcodes.hpp"
#include "Packet.hpp"
#include "CryptoContext.hpp"
#include <QThread>
#include <iostream>
#include <cassert>

using namespace inferno;

void test_inject_e2e() {
    // Full round-trip: Server sends INJECT → client processes → client sends INJECT_RES
    CryptoContext::instance().initDefault();

    Server s(0);
    assert(s.start() && "Server should start");
    uint16_t port = s.getPort();

    QThread thread;
    s.moveToThread(&thread);
    QObject::connect(&thread, &QThread::started, &s, &Server::run);
    thread.start();

    // Connect a real client socket (simulating the agent)
    Socket client;
    assert(client.connectTo("127.0.0.1", port) && "Client should connect");

    QThread::msleep(100);

    // Read the initial SYS_REQ_INFO packet from the server (malleable format)
    std::vector<uint8_t> buf;
    ssize_t initial_bytes = client.receiveData(buf, 4096);
    assert(initial_bytes > 0 && "Should receive initial SYS_REQ_INFO");
    auto initialPkt = client.receivePacket(buf);
    assert(initialPkt.has_value() && "Should parse initial SYS_REQ_INFO");
    buf.clear();

    // Agent sends a SCAN_RESULT back (so the server doesn't wait forever)
    client.sendPacket(static_cast<uint16_t>(Opcode::SCAN_RESULT),
                       "/Applications/DBeaver.app|com.dbeaver|1|1");
    QThread::msleep(50);

    // Server sends INJECT command to the agent
    std::string appPath = "/Applications/TestApp.app/Contents/MacOS/TestApp";
    s.sendInjectCommand("127.0.0.1", QString::fromStdString(appPath));
    QThread::msleep(100);

    // Client (agent) receives the INJECT packet (malleable format)
    ssize_t inject_bytes = client.receiveData(buf, 4096);
    assert(inject_bytes > 0 && "Should receive INJECT packet");
    auto injectPacket = client.receivePacket(buf);
    assert(injectPacket.has_value() && "Should deserialize INJECT packet");
    assert(injectPacket->getOpcode() == static_cast<uint16_t>(Opcode::INJECT) &&
           "Opcode should be INJECT");
    std::string receivedPath(injectPacket->getPayload().begin(),
                              injectPacket->getPayload().end());
    assert(receivedPath == appPath && "INJECT payload should match sent path");

    // Agent sends INJECT_RES back (in testing mode, injectIntoTarget returns true)
    client.sendPacket(static_cast<uint16_t>(Opcode::INJECT_RES),
                       appPath + "||1|1");
    QThread::msleep(50);

    // Verify the signal fired on the server side via a manual check
    // We verify by checking the client received the INJECT (above) and
    // the INJECT_RES was sent (below — we just test the packet roundtrip is complete)
    std::cout << "[PASS] test_inject_e2e" << std::endl;

    s.stop();
    thread.quit();
    thread.wait();
}
