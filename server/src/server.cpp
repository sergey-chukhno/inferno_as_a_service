#include "../include/server.hpp"
#include <iostream>
#include <algorithm> // std::remove_if

namespace inferno {

// ─── Constructors / Destructor ───────────────────────────────────────────────

Server::Server() : m_port(0), m_running(false) {}

Server::Server(uint16_t port) : m_port(port), m_running(false) {}

Server::~Server() {
    stop();
}

// ─── Core ─────────────────────────────────────────────────────────────────────

bool Server::start() {
    // 1. Bind le socket d'écoute sur toutes les interfaces (0.0.0.0)
    if (!m_listen_socket.bindNode("0.0.0.0", m_port)) {
        std::cerr << "[Server] bindNode() failed on port " << m_port << "\n";
        return false;
    }

    // 2. Passer en mode écoute
    if (!m_listen_socket.listen()) {
        std::cerr << "[Server] listen() failed\n";
        return false;
    }

    m_running = true;
    std::cout << "[Server] Listening on port " << m_port << "\n";
    return true;
}

void Server::run() {
    if (!m_running) {
        std::cerr << "[Server] Call start() before run()\n";
        return;
    }

    while (m_running) {

        // ── Étape 1 : construire le fd_set ───────────────────────────────────
        // fd_set est un ensemble de file descriptors que select() va surveiller.
        // On le reconstruit à chaque tour car select() le modifie.

        fd_set read_fds;
        FD_ZERO(&read_fds); // Vider l'ensemble

        // Ajouter le socket d'écoute
        FD_SET(m_listen_socket.getFd(), &read_fds);
        int max_fd = m_listen_socket.getFd(); // select() a besoin du fd le plus grand

        // Ajouter tous les clients connectés
        for (const auto& client : m_clients) {
            FD_SET(client.getFd(), &read_fds);
            if (client.getFd() > max_fd)
                max_fd = client.getFd();
        }

        // ── Étape 2 : appeler select() ───────────────────────────────────────
        // select() bloque jusqu'à ce qu'au moins un fd soit prêt à lire.
        // Arguments : max_fd + 1, fd_set lecture, fd_set écriture, fd_set erreur, timeout
        // On passe nullptr pour écriture/erreur et pas de timeout (bloque indéfiniment).

        int activity = ::select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);

        if (activity < 0) {
            std::cerr << "[Server] select() error\n";
            break;
        }

        // ── Étape 3 : nouveau client ? ───────────────────────────────────────
        // FD_ISSET vérifie si un fd spécifique est "prêt" dans le fd_set.

        if (FD_ISSET(m_listen_socket.getFd(), &read_fds)) {
            auto new_client = m_listen_socket.acceptNode();
            if (new_client.has_value()) {
                std::cout << "[Server] New client connected: "
                          << new_client->getIp() << ":"
                          << new_client->getPort() << "\n";
                // On déplace le Socket dans le vector (pas de copie)
                m_clients.push_back(std::move(*new_client));
            }
        }

        // ── Étape 4 : données d'un client existant ? ─────────────────────────
        // On itère sur les clients et on vérifie chacun.
        // On collecte les fd déconnectés pour les supprimer après l'itération.

        std::vector<int> to_remove;

        for (auto& client : m_clients) {
            if (FD_ISSET(client.getFd(), &read_fds)) {
                std::vector<uint8_t> buffer;
                ssize_t bytes = client.receiveData(buffer, 4096);

                if (bytes <= 0) {
                    // 0 = déconnexion propre, <0 = erreur
                    std::cout << "[Server] Client " << client.getIp() << " disconnected\n";
                    to_remove.push_back(client.getFd());
                } else {
                    // Pour l'instant : afficher ce qu'on reçoit (debug)
                    std::string msg(buffer.begin(), buffer.end());
                    std::cout << "[Server] Received from " << client.getIp()
                              << ": " << msg << "\n";
                }
            }
        }

        // ── Étape 5 : nettoyer les clients déconnectés ───────────────────────
        // On supprime du vector les sockets dont le fd est dans to_remove.
        // Le Socket est détruit → son destructeur ferme le fd automatiquement.

        m_clients.erase(
            std::remove_if(m_clients.begin(), m_clients.end(),
                [&to_remove](const Socket& s) {
                    return std::find(to_remove.begin(), to_remove.end(), s.getFd())
                           != to_remove.end();
                }),
            m_clients.end()
        );
    }
}

void Server::stop() {
    m_running = false;
    m_clients.clear(); // Détruit chaque Socket → ferme chaque fd
    // m_listen_socket est détruit par son propre destructeur
}

// ─── Getters ─────────────────────────────────────────────────────────────────

bool     Server::isRunning() const { return m_running; }
uint16_t Server::getPort()   const { return m_port; }

} // namespace inferno