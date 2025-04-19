# Distributed File System using Socket Programming

This project implements a basic **Distributed File System (DFS)** in C using socket programming. It was developed as part of the **COMP-8567: Operating Systems** course at the **University of Windsor** during **Summer 2025**.

## 🧱 Project Overview

The system mimics a multi-server file storage mechanism where:

- All **clients interact only with Server S1**
- Based on file extensions, S1 routes files to other backend servers:
  - `.c` → stored in **S1**
  - `.pdf` → routed to **S2**
  - `.txt` → routed to **S3**
  - `.zip` → routed to **S4**

Each server runs on a different terminal and communicates via sockets.

---

## 📁 Directory Structure

istributed-file-system/
│
├── client/
│   └── w25clients.c
│
├── servers/
│   ├── S1.c
│   ├── S2.c
│   ├── S3.c
│   └── S4.c
│
├── docs/
│   └── W25_Project.pdf
│
├── .gitignore
├── README.md
└── LICENSE (optional)

## 🧠 Supported Commands

```bash
uploadf filename destination_path
downlf filename
removef filename
downltar filetype
dispfnames pathname

## 🚀 Compilation

gcc -o S1 servers/S1.c
gcc -o S2 servers/S2.c
gcc -o S3 servers/S3.c
gcc -o S4 servers/S4.c
gcc -o client client/w25clients.c

## 🧪 Run Instructions

Open separate terminals for S1, S2, S3, and S4.

Run each server binary.

Then run the client and enter any of the supported commands.

## 📄 Documentation
Project description and command details can be found in docs/W25_Project.pdf.
