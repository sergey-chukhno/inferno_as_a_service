# 🔥 Les 9 Cercles de l’Enfer

> “L'enfer est trop petit pour tout le monde.” — Claire de Lamirande

---

## 🚪 L’Ante-Enfer

### Règles générales

- Respecter la forme canonique de Coplien
- Préférer références aux pointeurs
- Variables en `const` sauf justification
- ❌ Pas de variables globales
- Attributs en `private` + getters/setters
- Utiliser des smart pointers
- Utiliser des **tags Git** pour les étapes

---

## 🟢 1er Cercle — Les Limbes

### Objectif

Créer un système client-serveur en C++.

### Contraintes

- Classe `Socket` pour encapsuler les syscalls réseau
- ❌ Aucun syscall hors de cette classe
- ❌ Pas de `thread` ni `fork`

### Validation

- Le serveur gère plusieurs clients simultanément

---

## 🟡 2ème Cercle — Luxure

### Objectif

Créer un **protocole binaire évolutif**

### Contraintes

- Classe `Packet`
- Structure en classes/structs
- Documentation type **RFC**

---

## 🟠 3ème Cercle — Gourmandise

### Objectif

Transformer le client en agent capable de :

- Envoyer infos système (hostname, user, OS)
- Keylogger (text logs, streams/events)
- Liste des processus
- Exécution de commandes système

### Important

- Tout doit être défini dans le protocole

---

## 🟣 4ème Cercle — Avarice [COMPLETED]

### Objectif

Créer une GUI avec Qt

### Fonctionnalités

- Liste des clients connectés
- IP de chaque client
- Exécution des commandes
- Déconnexion client

---

## 🔴 5ème Cercle — Colère [COMPLETED]

### Objectif

Ajouter persistance + traitement forensic.

### Features

- Affichage en temps réel (Signal/Slot)
- Stockage en base PostgreSQL 16
- Classe `Inferno_Database` (Singleton)
- Hardware-based UUID Fingerprinting
- Loot persistence (BYTEA) pour exfiltration binaire
- Reconnexion automatique et tracking online/offline
- OPSEC-hardened configuration (.env loader)

---

## ⚫ 6ème Cercle — Hérésie [COMPLETED]

### Objectif

Analyse des données et extraction d'intelligence forensic en temps réel.

### Extraction et Traitement

- **Emails** : Capture via regex RFC-compliante.
- **Téléphones** : Validation dynamique avec élimination des faux-positifs (IPs, dates, timestamps).
- **Mots de passe** : Heuristiques contextuelles et détection des séquences `[TAB]credential[ENTER]`.
- **Cartes bancaires** : Filtrage numérique et validation par **Algorithme de Luhn**.
- **Backspace Filtering** : Reconstitution chronologique des frappes en exécutant rétroactivement les corrections `[BACKSPACE]`.
- **Sous-chaînes & Déduplication** : Fusion temps réel des séquences de saisies croissantes dans la base de données.

### Classes Clés

- `Analysis` (Extraction & Regex)
- `IntelAnalysisService` (Business logic singleton service)

---

## 🔥 7ème Cercle — Violence

### Objectif

Cross-platform (Windows + Linux)

### Contraintes

- Interfaces + implémentations par OS
- Utilisation du préprocesseur

---

## 🕶️ 8ème Cercle — Ruse et Tromperie

### Objectif

Rendre le client discret

### Features

- Cacher la console
- Wrapper
- Installation discrète
- Auto-start (bonus)
- Reconnexion automatique

---

## ❄️ 9ème Cercle — Trahison

### Objectif

Propagation (conceptuelle)

---

## 📦 Rendu

- Repo GitHub
- Client = `client`
- Serveur = `server`
- Compilation sans warnings
- Utilisation des tags

---

## 🧠 Compétences

- Architecture logicielle
- Réseau
- GUI
- Base de données
- DevOps

---

## 📚 Stack

- C++
- Winsock / sockets
- Qt
- PostgreSQL

---

## 🚀 Horizon — Enterprise Evolution Roadmap

Refer to the [Roadmap Forward](docs/ROADMAP_FORWARD.md) document for post-Circle 9 architectural, scalability, mTLS 1.3 transport, binary protocol encryption, EDR system call hook evasion, remote teamserver collaboration, and AI-enhanced forensics milestones.