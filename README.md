# mohh-ai-os
Mohh AI-OS est une exploration ambitieuse dans la création d'un système d'exploitation à partir de zéro, où l'intelligence artificielle n'est pas une surcouche applicative, mais le composant central de l'interaction homme-machine.

## Philosophie du Projet :

**IA Native** : L'intelligence artificielle ne sera pas une application tournant sur le système, mais le cœur même du système. Chaque interaction, de la gestion des fichiers à la connexion réseau, sera orchestrée par l'IA.

**Interaction Directe avec le Matériel** : En l'absence de noyau traditionnel sur lequel s'appuyer, Mohh AI-OS est conçu pour communiquer directement avec les composants matériels, offrant une performance et une intégration maximales.

Ce projet est actuellement dans une phase de développement active, construisant les fondations nécessaires pour réaliser cette vision.

## État Actuel du Projet
La version actuelle est un micro-noyau fonctionnel avec un shell en espace utilisateur, capable de lancer des programmes et de simuler une interaction avec une IA. Le système démarre, gère ses ressources de base et offre une interface en ligne de commande interactive.

## Fonctionnalités Implémentées
Le système d'exploitation dispose actuellement des fonctionnalités suivantes :

### Noyau et Démarrage

*   Noyau 32-bit écrit en C et Assembleur.
*   Amorçage via le standard Multiboot 1.
*   Contrôle direct de la mémoire vidéo VGA pour l'affichage en mode texte.

### Gestion des Interruptions

*   Mise en place et chargement d'une Table de Descripteurs d'Interruptions (IDT).
*   Gestion du Contrôleur d'Interruptions Programmable (PIC 8259A).
*   Pilote de clavier PS/2 de base pour la saisie utilisateur.

### Gestion de la Mémoire

*   Gestionnaire de mémoire physique (PMM) par bitmap.
*   Mise en place de la pagination et de la mémoire virtuelle (VMM), permettant d'isoler le noyau des autres tâches.

### Système de Tâches et Ordonnancement

*   Ordonnanceur préemptif basé sur un algorithme Round-Robin.
*   Gestionnaire de tâches capable d'exécuter plusieurs processus en parallèle.
*   Changement de contexte (Context Switching) pour le multitâche.

### Espace Utilisateur et Applications

*   Séparation claire entre l'espace noyau (Ring 0) et l'espace utilisateur (Ring 3).
*   Chargeur d'exécutables au format ELF 32-bit.
*   Implémentation d'appels système (Syscalls) pour permettre aux applications de communiquer avec le noyau.
*   Shell interactif (shell.bin) capable de lire les commandes de l'utilisateur.
*   Lancement de processus depuis le shell, avec une simulation de moteur d'IA (fake\_ai.bin).

### Système de Fichiers

*   Support pour un disque virtuel en RAM (initrd) au format tar, permettant de charger le shell et d'autres programmes au démarrage.

## Compilation et Exécution
### Prérequis
Avant de commencer, assurez-vous d'avoir les outils suivants :

*   `git`
*   `make`
*   `gcc` et `binutils` (le paquet `build-essential` sur Debian/Ubuntu)
*   `nasm` (Netwide Assembler)
*   `qemu-system-x86` (pour l'émulation)
*   `xorriso` (pour la création de l'image ISO, paquet `xorriso`)

### Sur Linux (Recommandé : Ubuntu, Debian)
C'est l'environnement de développement natif pour ce projet.

1.  **Cloner le dépôt :**
    ```bash
    git clone https://github.com/kamgueblondin/mohh-ai-os.git
    cd mohh-ai-os
    ```
2.  **Installer les dépendances :**
    ```bash
    sudo apt-get update
    sudo apt-get install build-essential nasm qemu-system-x86 xorriso
    ```
3.  **Compiler le système d'exploitation :**
    ```bash
    make
    ```
    Cette commande va compiler le noyau, les programmes en espace utilisateur, et créer un fichier `build/mohh-ai-os.bin`.
4.  **Exécuter dans l'émulateur QEMU :**
    ```bash
    make run
    ```
    QEMU se lancera et démarrera directement sur votre noyau. Vous devriez voir le shell de Mohh AI-OS apparaître.

### Sur Windows (via WSL2)
La meilleure façon de développer sur Windows est d'utiliser le Sous-système Windows pour Linux (WSL2).

1.  **Installer WSL2** : Suivez le guide officiel de Microsoft pour installer WSL2 et une distribution Linux (par exemple, Ubuntu).
2.  **Lancer votre terminal WSL2.**
3.  **Suivre les instructions pour Linux ci-dessus.** Toutes les commandes (`git`, `apt-get`, `make`) fonctionneront de la même manière à l'intérieur de WSL2. QEMU s'exécutera également dans cet environnement.

### Installation sur une Machine Virtuelle (VirtualBox)
Pour tester Mohh AI-OS comme un vrai système d'exploitation, vous pouvez l'installer sur une machine virtuelle comme VirtualBox.

1.  **Compiler le projet :**
    Assurez-vous d'avoir compilé le projet avec `make` comme décrit ci-dessus.
2.  **Créer l'image ISO bootable :**
    Utilisez la commande suivante à la racine du projet. Elle créera un fichier `mohh-ai-os.iso`.
    ```bash
    make iso
    ```
3.  **Configurer VirtualBox :**
    *   Ouvrez VirtualBox et cliquez sur "Nouvelle".
    *   **Nom** : `Mohh AI-OS`
    *   **Type** : `Other`
    *   **Version** : `Other/Unknown`
    *   **Mémoire vive** : Allouez au moins 128 Mo.
    *   **Disque dur** : Vous pouvez choisir "Ne pas ajouter de disque dur virtuel" pour l'instant.
    *   Cliquez sur "Créer".
4.  **Monter l'image ISO :**
    *   Sélectionnez votre nouvelle machine virtuelle et allez dans "Configuration".
    *   Allez dans la section "Stockage".
    *   Sous "Contrôleur : IDE", cliquez sur le lecteur de CD/DVD qui est "Vide".
    *   Sur la droite, cliquez sur l'icône de CD et choisissez "Choisir un fichier de disque".
    *   Naviguez jusqu'à votre projet et sélectionnez le fichier `mohh-ai-os.iso` que vous avez créé.
    *   Cliquez sur "OK".
5.  **Démarrer la machine virtuelle :**
    *   Démarrez la VM. Elle devrait maintenant booter directement sur Mohh AI-OS.

## Feuille de Route (Prochaines Étapes)
La vision pour Mohh AI-OS est encore loin d'être complète. Les prochaines grandes étapes de développement sont :

*   **Système de Fichiers sur Disque Réel** : Implémenter un pilote AHCI pour les disques SATA et un système de fichiers (ex: FAT32) pour un stockage permanent.
*   **Bibliothèque C Standard (Libc)** : Porter ou développer une libc pour faciliter la compilation d'applications complexes (comme un vrai moteur d'IA).
*   **Pile Réseau** : Développer des pilotes pour les cartes réseau et une pile TCP/IP pour la communication.
*   **Pilotes Graphiques (GPU)** : Passer d'un mode texte à un mode graphique (via VBE/GOP) comme première étape vers l'accélération matérielle.

## Contribution
Ce projet est une entreprise d'apprentissage et d'expérimentation. Toute contribution, rapport de bug ou suggestion est la bienvenue. N'hésitez pas à ouvrir une "Issue" ou une "Pull Request".

## Help Command
Compilation et Exécution Prérequis Avant de commencer, assurez-vous d'avoir les outils suivants :

git make gcc, grub-pc-bin et binutils (le paquet build-essential sur Debian/Ubuntu) nasm (Netwide Assembler) qemu-system-x86 (pour l'émulation) xorriso (pour la création de l'image ISO, paquet xorriso) Sur Linux (Recommandé : Ubuntu, Debian) C'est l'environnement de développement natif pour ce projet.

Compiler le projet : Assurez-toi d'avoir compilé le projet avec make. Créer l'image ISO bootable(make iso). Elle créera un fichier mohh-ai-os.iso met le dans un dossier release.
