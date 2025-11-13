/*
 * LOQUI SERVER (Hito H-3 Completado + Historial)
 * Creado para Windows y CLion.
 *
 * Implementación del Hito H-3 (Multithreading) y añadido
 * de persistencia de mensajes (Historial).
 * - USA HASHING: SHA-256 + Salting (vía picosha2.h).
 * - USA PERSISTENCIA: Usuarios en "users.csv", Mensajes en "history.csv".
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <sstream>
#include <fstream> // Para persistencia
#include <random> // Para generar 'salt'
#include <chrono> // Para timestamps
#include "picosha2.h" // Para Hashing SHA-256

// --- Estructuras de Datos (Completas) ---

// Almacén de usuarios
struct UserData {
    std::string salt;
    std::string hash; // hash(password + salt)
};
std::map<std::string, UserData> g_userStore;
std::mutex g_userStoreMutex; // Mutex para proteger g_userStore

// Clientes conectados (username, socket)
std::map<std::string, SOCKET> g_connectedClients;
std::mutex g_clientsMutex; // Mutex para proteger g_connectedClients

const std::string USER_FILE = "users.csv"; // Archivo de persistencia de usuarios
const std::string HISTORY_FILE = "history.csv"; // Archivo de persistencia de mensajes

// --- Prototipos de Funciones ---
void handleClient(SOCKET clientSocket);
std::vector<std::string> split(const std::string& s, char delimiter);
void sendResponse(SOCKET clientSocket, const std::string& response); // NUEVO: Añade \n y envía
void sendMessageToClient(const std::string& fromUser, const std::string& toUser, const std::string& chatMessage);
void loadUsers();
void saveUser(const std::string& username, const UserData& data);
std::string generateSalt(int length = 16);
std::string getCurrentTimestamp();
void saveMessage(const std::string& sender, const std::string& receiver, const std::string& timestamp, const std::string& message);
void sendHistoryToClient(SOCKET clientSocket, const std::string& currentUser, const std::string& otherUser);

int main() {
    WSADATA wsaData;
    int iResult;

    // 1. Inicializar Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // *** INICIO HITO H-2: Cargar usuarios desde el archivo ***
    loadUsers();
    // *** FIN HITO H-2 ***

    // 2. Crear Socket del Servidor
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 3. Configurar dirección y puerto (usaremos 12345)
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Escuchar en todas las interfaces
    serverAddr.sin_port = htons(12345);

    // 4. Bind
    iResult = bind(listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // 5. Listen
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "[LoquiServer] Servidor iniciado en el puerto 12345." << std::endl;
    std::cout << "[LoquiServer] Esperando conexiones..." << std::endl;

    // 6. Bucle de Aceptación de Clientes
    SOCKET clientSocket;
    while (true) {
        clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            continue; // Continuar escuchando
        }

        std::cout << "[LoquiServer] Nuevo cliente conectado." << std::endl;

        // Crear un hilo para manejar a este cliente (Hito H-3)
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach(); // El hilo se ejecutará de forma independiente
    }

    // (Nunca se alcanza en este bucle, pero es buena práctica)
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}

// Función que se ejecuta en un hilo para cada cliente
void handleClient(SOCKET clientSocket) {
    char recvbuf[512];
    int iResult;
    std::string currentUsername; // Nombre del usuario logueado en este hilo

    // Bucle de recepción de mensajes del cliente
    while ((iResult = recv(clientSocket, recvbuf, sizeof(recvbuf), 0)) > 0) {
        std::string message(recvbuf, iResult);
        std::cout << "[LoquiServer] Recibido: " << message << std::endl;

        std::vector<std::string> parts = split(message, '|');
        if (parts.empty()) continue;

        std::string cmd = parts[0];
        std::string response;

        // --- Procesamiento del Protocolo (RF-1.0 a RF-6.0) ---

        if (cmd == "REGISTER" && parts.size() == 3) {
            // RF-1.0: REGISTRO (CON HASHING Y PERSISTENCIA)
            std::string user = parts[1];
            std::string pass_plain = parts[2];

            std::lock_guard<std::mutex> lock(g_userStoreMutex);
            if (g_userStore.find(user) == g_userStore.end()) {
                // 1. Generar Salt
                std::string salt = generateSalt();
                // 2. Calcular Hash
                std::string hash = picosha2::hash256_hex_string(pass_plain + salt);

                // 3. Guardar en memoria
                UserData newUser = {salt, hash};
                g_userStore[user] = newUser;

                // 4. Guardar en archivo (Persistencia)
                saveUser(user, newUser);

                response = "RESP|OK|Usuario registrado con exito.";
            } else {
                response = "RESP|ERROR|El nombre de usuario ya existe.";
            }
            sendResponse(clientSocket, response); // USAR NUEVO HELPER

        } else if (cmd == "LOGIN" && parts.size() == 3) {
            // RF-2.0: INICIO DE SESIÓN (CON HASHING)
            std::string user = parts[1];
            std::string pass_plain = parts[2];

            bool authSuccess = false;
            {
                std::lock_guard<std::mutex> lock(g_userStoreMutex);
                auto it = g_userStore.find(user);
                if (it != g_userStore.end()) {
                    // Usuario encontrado, verificar contraseña
                    std::string storedSalt = it->second.salt;
                    std::string storedHash = it->second.hash;

                    // 1. Calcular hash del intento
                    std::string attemptHash = picosha2::hash256_hex_string(pass_plain + storedSalt);

                    // 2. Comparar
                    if (attemptHash == storedHash) {
                        authSuccess = true;
                    }
                }
                // Si el usuario no existe (it == g_userStore.end()), authSuccess sigue false
            }

            if (authSuccess) {
                std::lock_guard<std::mutex> lock(g_clientsMutex);
                // Verificar si ya está conectado
                if (g_connectedClients.find(user) != g_connectedClients.end()) {
                    response = "RESP|ERROR|Usuario ya esta conectado.";
                } else {
                    g_connectedClients[user] = clientSocket;
                    currentUsername = user; // Asignar usuario a este hilo
                    response = "RESP|OK|Login exitoso.";
                    std::cout << "[LoquiServer] Usuario " << user << " ha iniciado sesion." << std::endl;
                }
            } else {
                response = "RESP|ERROR|Credenciales incorrectas.";
            }
            sendResponse(clientSocket, response); // USAR NUEVO HELPER

        } else if (cmd == "MSG" && parts.size() >= 3 && !currentUsername.empty()) {
            // RF-3.0 & RF-4.0: ENVÍO/RECEPCIÓN DE MENSAJES (AHORA CON TIMESTAMP)
            std::string toUser = parts[1];
            std::string chatMessage = parts[2];
            // Reconstruir el mensaje si tenía '|'
            for (size_t i = 3; i < parts.size(); ++i) {
                chatMessage += "|" + parts[i];
            }

            sendMessageToClient(currentUsername, toUser, chatMessage);

        } else if (cmd == "LIST" && !currentUsername.empty()) {
            // RF-5.0: LISTADO DE USUARIOS
            response = "LIST_RESP";
            std::lock_guard<std::mutex> lock(g_clientsMutex);
            for (auto const& [user, sock] : g_connectedClients) {
                response += "|" + user;
            }
            sendResponse(clientSocket, response); // USAR NUEVO HELPER

        } else if (cmd == "HISTORY" && parts.size() == 2 && !currentUsername.empty()) {
            // NUEVO: RF-7.0 (IMPLÍCITO): SOLICITAR HISTORIAL DE CONVERSACIÓN
            std::string otherUser = parts[1];
            sendHistoryToClient(clientSocket, currentUsername, otherUser);

        } else if (cmd == "DC") {
            // RF-6.0: CIERRE DE SESIÓN
            break; // Salir del bucle
        }

    } // Fin del bucle while(recv)

    // --- Desconexión del Cliente ---
    std::cout << "[LoquiServer] Cliente desconectado." << std::endl;
    if (!currentUsername.empty()) {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_connectedClients.erase(currentUsername);
        std::cout << "[LoquiServer] Usuario " << currentUsername << " ha cerrado sesion." << std::endl;
    }
    closesocket(clientSocket);
}

// NUEVA: Agrega el delimitador de fin de mensaje y lo envía
void sendResponse(SOCKET clientSocket, const std::string& response) {
    // Añadimos un delimitador de nueva línea para indicar el final del mensaje
    std::string delimitedResponse = response + "\n";
    send(clientSocket, delimitedResponse.c_str(), delimitedResponse.length(), 0);
}

// Función auxiliar para enviar un mensaje a un usuario específico
void sendMessageToClient(const std::string& fromUser, const std::string& toUser, const std::string& chatMessage) {
    std::string timestamp = getCurrentTimestamp();
    // Formato: "MSG|timestamp|fromUser|chatMessage"
    std::string fullMessage = "MSG|" + timestamp + "|" + fromUser + "|" + chatMessage;

    // 1. Persistir el mensaje
    saveMessage(fromUser, toUser, timestamp, chatMessage);

    // 2. Intentar enviar al destinatario
    SOCKET targetSocket = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        auto it = g_connectedClients.find(toUser);
        if (it != g_connectedClients.end()) {
            targetSocket = it->second;
        }
    }

    if (targetSocket != INVALID_SOCKET) {
        sendResponse(targetSocket, fullMessage); // USAR NUEVO HELPER
        std::cout << "[LoquiServer] Enviando " << fullMessage << " a " << toUser << std::endl;
    } else {
        std::cout << "[LoquiServer] Usuario " << toUser << " no conectado. Mensaje guardado." << std::endl;
        // Opcional: enviar un "RESP|ERROR|Usuario no conectado" al remitente
    }
}

// Función auxiliar para dividir strings
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Genera un 'salt' aleatorio de longitud 'length'
std::string generateSalt(int length) {
    const std::string CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, CHARACTERS.length() - 1);

    std::string salt = "";
    for(int i = 0; i < length; ++i) {
        salt += CHARACTERS[distribution(generator)];
    }
    return salt;
}

// Guarda un nuevo usuario en el archivo CSV (modo append)
void saveUser(const std::string& username, const UserData& data) {
    // No necesitamos mutex aquí si solo la llamamos desde
    // 'handleClient' DENTRO de un 'lock' existente.
    std::ofstream file(USER_FILE, std::ios::app); // app = append
    if (file.is_open()) {
        file << username << "," << data.salt << "," << data.hash << "\n";
        file.close();
    } else {
        std::cerr << "[LoquiServer] ERROR: No se pudo abrir " << USER_FILE << " para escritura." << std::endl;
    }
}

// Carga todos los usuarios desde el archivo CSV al inicio
void loadUsers() {
    std::ifstream file(USER_FILE);
    if (!file.is_open()) {
        std::cout << "[LoquiServer] No se encontro " << USER_FILE << ". Se creara uno nuevo al primer registro." << std::endl;
        return;
    }

    std::string line;
    int count = 0;

    std::lock_guard<std::mutex> lock(g_userStoreMutex);
    while (std::getline(file, line)) {
        std::vector<std::string> parts = split(line, ',');
        if (parts.size() == 3) {
            // Formato: username,salt,hash
            g_userStore[parts[0]] = {parts[1], parts[2]};
            count++;
        }
    }

    file.close();
    std::cout << "[LoquiServer] Cargados " << count << " usuarios desde " << USER_FILE << "." << std::endl;
}

// NUEVA: Genera una marca de tiempo en formato YYYY-MM-DD HH:MM:SS
std::string getCurrentTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt = system_clock::to_time_t(now);

    std::tm tm_buf;
    // Usar localtime_s en Windows
    #ifdef _WIN32
        localtime_s(&tm_buf, &tt);
    #else
        // Opcional: usar localtime_r para otros sistemas si se compila allí
        // localtime_r(&tt, &tm_buf);
        std::tm* tm_ptr = std::localtime(&tt);
        if (tm_ptr) tm_buf = *tm_ptr;
        else return "Timestamp Error"; // En caso de fallo
    #endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// NUEVA: Guarda un mensaje en el archivo de historial
// Formato: timestamp,sender,receiver,"message"
void saveMessage(const std::string& sender, const std::string& receiver, const std::string& timestamp, const std::string& message) {
    // Nota: Aunque el servidor es multithread, ofstream/fstream manejan
    // la exclusión mutua a nivel de sistema operativo para writes a archivos.
    // Usaremos un mutex simple para ser más explícitos si fuera necesario,
    // pero para este simple append es generalmente seguro.
    std::ofstream file(HISTORY_FILE, std::ios::app);
    if (file.is_open()) {
        // Encerramos el mensaje en comillas dobles para que los delimitadores internos (',') no rompan el CSV
        file << timestamp << "," << sender << "," << receiver << ",\"" << message << "\"\n";
        file.close();
    } else {
        std::cerr << "[LoquiServer] ERROR: No se pudo abrir " << HISTORY_FILE << " para escritura." << std::endl;
    }
}

// NUEVA: Envía el historial de mensajes entre dos usuarios al cliente
void sendHistoryToClient(SOCKET clientSocket, const std::string& currentUser, const std::string& otherUser) {
    std::ifstream file(HISTORY_FILE);
    if (!file.is_open()) {
        std::string resp = "RESP|OK|No hay historial de mensajes.";
        sendResponse(clientSocket, resp); // USAR NUEVO HELPER
        return;
    }

    std::string historyResponse = "HISTORY_RESP|" + otherUser; // HISTORY_RESP|otherUser|
    std::string line;
    int count = 0;

    while (std::getline(file, line)) {
        // El formato es: timestamp,sender,receiver,"message"
        // Como el mensaje puede contener comas, usamos una lógica simple
        // para extraer las primeras 3 partes y el resto como mensaje.

        std::stringstream ss(line);
        std::string part;
        std::vector<std::string> parts;

        // Extraer timestamp
        if (std::getline(ss, part, ',')) parts.push_back(part);
        // Extraer sender
        if (std::getline(ss, part, ',')) parts.push_back(part);
        // Extraer receiver
        if (std::getline(ss, part, ',')) parts.push_back(part);

        // El resto de la línea es el mensaje (que empieza con una comilla doble)
        std::string messageContent;
        if (std::getline(ss, messageContent)) {
            // Eliminar las comillas dobles si existen
            if (!messageContent.empty() && messageContent.front() == '"' && messageContent.back() == '"') {
                 messageContent = messageContent.substr(1, messageContent.length() - 2);
            }
            parts.push_back(messageContent);
        }

        if (parts.size() == 4) {
            std::string timestamp = parts[0];
            std::string sender = parts[1];
            std::string receiver = parts[2];
            std::string message = parts[3];

            // Revisar si la conversación es entre currentUser y otherUser (en ambos sentidos)
            bool isRelevant = (sender == currentUser && receiver == otherUser) ||
                              (sender == otherUser && receiver == currentUser);

            if (isRelevant) {
                // Formato de respuesta: HISTORY_RESP|otherUser|timestamp|sender|message
                historyResponse += "|" + timestamp + "|" + sender + "|" + message;
                count++;
            }
        }
    }

    file.close();

    if (count > 0) {
        sendResponse(clientSocket, historyResponse); // USAR NUEVO HELPER
    } else {
        std::string resp = "RESP|OK|No hay historial de mensajes con " + otherUser + ".";
        sendResponse(clientSocket, resp); // USAR NUEVO HELPER
    }

    std::cout << "[LoquiServer] Enviado historial con " << count << " mensajes para " << currentUser << " con " << otherUser << "." << std::endl;
}
