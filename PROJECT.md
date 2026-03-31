# 🔥 Les 9 Cercles de l’Enfer

> “L'enfer est trop petit pour tout le monde.” — Claire de Lamirande

---

## 🌑 Introduction

La structure de l’Enfer est composée de neuf cercles en spirale jusqu’au centre de la Terre.

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

## 🟣 4ème Cercle — Avarice

### Objectif
Créer une GUI avec Qt

### Fonctionnalités
- Liste des clients connectés
- IP de chaque client
- Exécution des commandes
- Déconnexion client

---

## 🔴 5ème Cercle — Colère

### Objectif
Ajouter persistance + traitement

### Features
- Affichage en temps réel
- Stockage en base PostgreSQL
- Classe `LPTF_Database`

### Bonus
- Clients online/offline visibles

---

## ⚫ 6ème Cercle — Hérésie

### Objectif
Analyse des données

### Extraction :
- Emails
- Téléphones
- Mots de passe
- Cartes bancaires

### Classe
- `Analysis`

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