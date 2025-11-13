/*
 * LOQUI CLIENT (Versión Simplificada)
 * Creado para Windows y CLion.
 *
 * Cliente de línea de comandos (CLI) para el servidor Loqui.
 * Utiliza dos hilos:
 * 1. Hilo Principal: Para enviar comandos (Login, Msg, List, etc.)
 * 2. Hilo Receptor: Para escuchar permanentemente al servidor (RF-4.0)
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream> // <-- AÑADIR ESTA LÍNEA
#include <algorithm>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

// Prototipos
void receiveMessages(SOCKET serverSocket);
std::vector<std::string> split(const std::string& s, char delimiter);

// Variable global para controlar el hilo receptor
bool g_running = true;
std::string g_currentChatUser = "";

void setupConsole() {
#ifdef _WIN32
    // Configurar la consola para UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Configurar fuente que soporte Unicode
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fontInfo;
    fontInfo.cbSize = sizeof(fontInfo);
    GetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
    wcscpy(fontInfo.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
#endif
}
void chatSession(SOCKET serverSocket, const std::string& targetUser) {
    std::cout << "\n";
    std::cout << "┌──────────────────────────────────────────┐" << std::endl;
    std::cout << "│              💬 CHAT CON " << targetUser;
    // Añadir espacios para alinear
    for (int i = targetUser.length(); i < 12; i++) std::cout << " ";
    std::cout << "│" << std::endl;
    std::cout << "└──────────────────────────────────────────┘" << std::endl;
    std::cout << "💡 Comandos: /salir, /historial, /limpiar" << std::endl;
    std::cout << "────────────────────────────────────────────" << std::endl;

    g_currentChatUser = targetUser;
    std::string message;

    while (g_running && g_currentChatUser == targetUser) {
        std::cout << "\n┌─[" << targetUser << "]\n";
        std::cout << "└─➤ ";
        std::getline(std::cin, message);

        if (message.empty()) continue;

        // Comando para salir del chat
        if (message == "/salir" || message == "/exit") {
            break;
        }

        // Comando para ver historial
        if (message == "/historial") {
            std::string request = "HISTORY|" + targetUser;
            send(serverSocket, request.c_str(), request.length(), 0);
            continue;
        }

        // Comando para limpiar pantalla
        if (message == "/limpiar" || message == "/clear") {
            #ifdef _WIN32
                system("cls");
            #else
                system("clear");
            #endif
            // Redibujar la cabecera
            std::cout << "\n";
            std::cout << "┌──────────────────────────────────────────┐" << std::endl;
            std::cout << "│              💬 CHAT CON " << targetUser;
            for (int i = targetUser.length(); i < 20; i++) std::cout << "  ";
            std::cout << "│" << std::endl;
            std::cout << "└──────────────────────────────────────────┘" << std::endl;
            continue;
        }

        // Enviar mensaje normal
        std::string request = "MSG|" + targetUser + "|" + message;
        send(serverSocket, request.c_str(), request.length(), 0);
    }

    g_currentChatUser = "";
    std::cout << "\n";
    std::cout << "┌──────────────────────────────────────────┐" << std::endl;
    std::cout << "│             🚪 CHAT FINALIZADO.           │" << std::endl;
    std::cout << "└──────────────────────────────────────────┘" << std::endl;
    std::cout << "> " << std::flush;
}

int main() {
    // Configurar consola para Unicode
    setupConsole();

    WSADATA wsaData;
    int iResult;

    // 1. Inicializar Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // 2. Crear Socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 3. Configurar Dirección del Servidor (localhost:12345)
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    // Usaremos "127.0.0.1" (localhost)
    iResult = inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    if (iResult <= 0) {
        std::cerr << "inet_pton failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 4. Conectar al Servidor
    iResult = connect(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "connect() failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "--- Comandos Disponibles ---" << std::endl;
    std::cout << "register <usuario> <pass>" << std::endl;
    std::cout << "login <usuario> <pass>" << std::endl;
    std::cout << "msg <usuario_destino> <mensaje>" << std::endl;
    std::cout << "chat <usuario_destino>     <- NUEVO: Sesión de chat continua" << std::endl;
    std::cout << "historial <usuario>        <- Ver historial de mensajes" << std::endl;
    std::cout << "list" << std::endl;
    std::cout << "exit" << std::endl;
    std::cout << "----------------------------" << std::endl;

    // 5. Iniciar hilo receptor (RF-4.0)
    std::thread receiverThread(receiveMessages, serverSocket);

    // 6. Bucle de envío (Hilo Principal)
    std::string line;
    while (g_running) {
        std::cout << "> ";
        std::getline(std::cin, line);

        if (line.empty()) continue;

        std::vector<std::string> parts = split(line, ' ');
        if (parts.empty()) continue;

        std::string cmd = parts[0];
        std::string request;

        // Formatear mensaje del protocolo
        if (cmd == "register" && parts.size() == 3) {
            request = "REGISTER|" + parts[1] + "|" + parts[2];
        } else if (cmd == "login" && parts.size() == 3) {
            request = "LOGIN|" + parts[1] + "|" + parts[2];
        } else if (cmd == "msg" && parts.size() >= 3) {
            request = "MSG|" + parts[1] + "|";
            // Reconstruir el mensaje
            for (size_t i = 2; i < parts.size(); ++i) {
                request += parts[i] + (i == parts.size() - 1 ? "" : " ");
            }
        } else if (cmd == "chat" && parts.size() == 2) {
            // NUEVO COMANDO: Iniciar sesión de chat
            std::string targetUser = parts[1];
            chatSession(serverSocket, targetUser);
            continue; // Importante: continuar sin enviar request
        } else if (cmd == "list") {
            request = "LIST";
        } else if (cmd == "historial" && parts.size() == 2) {
            // Comando para ver historial sin entrar en chat
            request = "HISTORY|" + parts[1];
        } else if (cmd == "exit") {
            request = "DC"; // Disconnect
            g_running = false;
        } else {
            std::cout << "Comando no reconocido." << std::endl;
            continue;
        }


        // Enviar comando al servidor
        send(serverSocket, request.c_str(), request.length(), 0);

        if (cmd == "exit") {
            break;
        }
    }

    // 7. Limpieza
    receiverThread.join(); // Esperar a que el hilo receptor termine
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}

// Hilo para recibir mensajes del servidor
// Hilo para recibir mensajes del servidor
void receiveMessages(SOCKET serverSocket) {
    char recvbuf[512];
    int iResult;

    while (g_running) {
        iResult = recv(serverSocket, recvbuf, sizeof(recvbuf), 0);

        if (iResult > 0) {
            std::string message(recvbuf, iResult);

            // Borrar la línea actual ("> ") para imprimir limpiamente
            std::cout << "\r" << std::flush;

            // --- Procesamiento de Respuestas del Servidor ---
            std::vector<std::string> parts = split(message, '|');
            if (parts.empty()) continue;

            std::string type = parts[0];

            if (type == "RESP") {
                // RESP|OK|Mensaje... o RESP|ERROR|Mensaje...
                if (parts.size() >= 3) {
                    std::cout << "[Servidor " << parts[1] << "]: " << parts[2] << std::endl;
                }
            } else if (type == "MSG") {
                // NUEVO FORMATO: MSG|timestamp|deUsuario|Mensaje...
                if (parts.size() >= 4) {
                    std::string timestamp = parts[1];
                    std::string fromUser = parts[2];
                    std::string chatMsg = parts[3];

                    // Reconstruir el mensaje si tenía '|' en el contenido
                    for (size_t i = 4; i < parts.size(); ++i) {
                        chatMsg += "|" + parts[i];
                    }

                    // Formato mejorado para mensajes entrantes
                    if (!g_currentChatUser.empty() && fromUser == g_currentChatUser) {
                        // Mensaje del usuario con el que estamos chateando ACTUALMENTE
                        std::cout << "┌─[" << timestamp << "] " << fromUser << "\n";
                        std::cout << "│ " << chatMsg << "\n";
                        std::cout << "└──────────────────────────────────────────\n";
                        std::cout << "┌─[" << g_currentChatUser << "]\n";  // <-- AÑADE ESTA LÍNEA
                        std::cout << "└─➤ " << std::flush;  // <-- Y ESTA
                    } else if (!g_currentChatUser.empty()) {
                        // Mensaje de OTRO usuario mientras estamos en chat con alguien
                        std::cout << "┌─🚨 MENSAJE DE " << fromUser << "\n";
                        std::cout << "│ [" << timestamp << "]\n";
                        std::cout << "│ " << chatMsg << "\n";
                        std::cout << "└──────────────────────────────────────────\n";
                        std::cout << "┌─[" << g_currentChatUser << "]\n";  // <-- AÑADE ESTA LÍNEA
                        std::cout << "└─➤ " << std::flush;  // <-- Y ESTA
                    } else {
                        // Mensaje recibido cuando NO estamos en un chat activo
                        std::cout << "┌─[" << timestamp << "] " << fromUser << "\n";
                        std::cout << "│ " << chatMsg << "\n";
                        std::cout << "└──────────────────────────────────────────\n";
                        std::cout << "> " << std::flush;  // Prompt normal
                    }
                }
            } else if (type == "HISTORY_RESP") {

    // HISTORY_RESP|otherUser|timestamp1|sender1|message1|timestamp2|sender2|message2...
    if (parts.size() >= 2) {
        std::string otherUser = parts[1];
        std::cout << "\n";
        std::cout << "┌──────────────────────────────────────────┐" << std::endl;
        std::cout << "│           📜 HISTORIAL CON " << otherUser;
        // Añadir espacios para alinear
        for (int i = otherUser.length(); i < 10; i++) std::cout << " ";
        std::cout << "│" << std::endl;
        std::cout << "└──────────────────────────────────────────┘" << std::endl;

        if (parts.size() >= 5) {
            int messageCount = 0;
            for (size_t i = 2; i + 2 < parts.size(); i += 3) {
                std::string timestamp = parts[i];
                std::string sender = parts[i+1];
                std::string msg = parts[i+2];

                // Determinar si el mensaje es propio o del otro usuario
                if (sender == otherUser) {
                    // Mensaje del otro usuario
                    std::cout << "┌─[" << timestamp << "] " << otherUser << "\n";
                    std::cout << "│ " << msg << "\n";
                } else {
                    // Mensaje propio
                    std::cout << "┌─[" << timestamp << "] 🟢 Tú\n";
                    std::cout << "│ " << msg << "\n";
                }
                std::cout << "└──────────────────────────────────────────" << std::endl;
                messageCount++;
            }
            std::cout << "📊 Total: " << messageCount << " mensajes" << std::endl;
        } else {
            std::cout << "📭 No hay mensajes en el historial." << std::endl;
        }

        std::cout << "──────────────────────────────────────────" << std::endl;

        // Reimprimir el prompt apropiado
        if (!g_currentChatUser.empty()) {
            std::cout << "┌─[" << g_currentChatUser << "]\n";
            std::cout << "└─➤ " << std::flush;
        } else {
            std::cout << "> " << std::flush;
        }


                }
            } else if (type == "LIST_RESP") {
                // LIST_RESP|userA|userB...
                std::cout << "[Usuarios Conectados]: ";
                for (size_t i = 1; i < parts.size(); ++i) {
                    std::cout << parts[i] << (i == parts.size() - 1 ? "" : ", ");
                }
                std::cout << std::endl;
            } else {
                std::cout << "[Servidor]: " << message << std::endl;
            }

            std::cout << "> " << std::flush; // Reimprimir el prompt

        } else if (iResult == 0) {
            std::cout << "\r[Conexion cerrada por el servidor]" << std::endl;
            g_running = false;
            break;
        } else {
            // Error si g_running es false (salida intencional)
            if (g_running) {
                std::cerr << "\rrecv() failed: " << WSAGetLastError() << std::endl;
            }
            g_running = false;
            break;
        }
    }
}

// Función auxiliar para dividir strings (simple)
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    // Usamos ' ' para la entrada del usuario, pero el server usa '|'
    // CORREGIDO: Usar 'token' en lugar de 'segment' (que estaba en la v. anterior)
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}