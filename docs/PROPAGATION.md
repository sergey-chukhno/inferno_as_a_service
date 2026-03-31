# 🦠 Propagation Strategy (Trahison)

## 1. Overview
The 9th Cercle requires "conceptual" propagation. For demonstration and safety purposes, we will construct a "controlled" lateral movement strategy alongside a dropper payload concept. 

**Safety Constraint**: The agent will attempt active network propagation *only* to a designated isolated subnet (e.g., a local Virtual Machine or Docker network) to avoid accidental real-world infection.

## 2. Infection Chain Vector (Lateral Movement)
1. **Discovery**: The agent executes an ARP scan or TCP port scan on the designated subnet. It identifies alive hosts.
2. **Vulnerability Assessment**: It attempts to connect to port 22 (SSH) or 445 (SMB) on the discovered hosts.
3. **Exploitation**: 
   - *SSH Vector*: It uses a hardcoded list of dummy credentials (e.g., `root:toor`) to attempt an automated login.
4. **Deployment**: If successful, it uploads a self-replica of the Agent binary via SCP/SFTP.
5. **Execution**: It executes the binary remotely, causing the new host to connect back to the C2 Server.

## 3. Trojan/Dropper Vector (Phishing/Social Engineering)
As an alternative strategy simulating real-world Trahison (Treason), we can build a **Dropper**.
1. **File Binding**: The Client binary is hidden within an ostensibly benign file (such as a PDF installer or a fake software update executable).
2. **Execution**: When the user opens the "PDF" or file, the dropper:
   - Silently extracts the Client binary to a temporary/hidden directory (e.g., `%APPDATA%` on Windows).
   - Executes the Client binary in the background.
   - Opens an actual benign PDF document so the user suspects nothing.
3. **Persistence**: The newly executed agent establishes its registry run keys for auto-start.
