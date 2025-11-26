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

using namespace std;

// Almacén de usuarios
struct UserData {
    string salt;
    string hash; // hash(password + salt)
};
map<string, UserData> g_userStore;
mutex g_userStoreMutex; // Mutex para proteger g_userStore

// Clientes conectados (username, socket)
map<string, SOCKET> g_connectedClients;
mutex g_clientsMutex; // Mutex para proteger g_connectedClients

const string USER_FILE = "users.csv"; // Archivo de persistencia de usuarios
const string HISTORY_FILE = "history.csv"; // Archivo de persistencia de mensajes

// --- Prototipos de Funciones ---
void handleClient(SOCKET clientSocket);
vector<string> split(const string& s, char delimiter);
void sendResponse(SOCKET clientSocket, const std::string& response); // NUEVO: Añade \n y envía
void sendMessageToClient(const std::string& fromUser, const std::string& toUser, const std::string& chatMessage);
void loadUsers();
void saveUser(const std::string& username, const UserData& data);
string generateSalt(int length = 16);
string getCurrentTimestamp();
void saveMessage(const std::string& sender, const std::string& receiver, const std::string& timestamp, const std::string& message);
void sendHistoryToClient(SOCKET clientSocket, const std::string& currentUser, const std::string& otherUser);

int main() {
    WSADATA wsaData;
    int iResult;

    // 1. Inicializar Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    // *** INICIO HITO H-2: Cargar usuarios desde el archivo ***
    loadUsers();
    // *** FIN HITO H-2 ***

    // 2. Crear Socket del Servidor
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
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
        cerr << "bind failed: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // 5. Listen
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "listen failed: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "[LoquiServer] Servidor iniciado en el puerto 12345." << std::endl;
    cout << "[LoquiServer] Esperando conexiones..." << std::endl;

    // 6. Bucle de Aceptación de Clientes
    SOCKET clientSocket;
    while (true) {
        clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "accept failed: " << WSAGetLastError() << endl;
            continue; // Continuar escuchando
        }

        cout << "[LoquiServer] Nuevo cliente conectado." << endl;

        // Crear un hilo para manejar a este cliente (Hito H-3)
        thread clientThread(handleClient, clientSocket);
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
    string currentUsername; // Nombre del usuario logueado en este hilo

    // Bucle de recepción de mensajes del cliente
    while ((iResult = recv(clientSocket, recvbuf, sizeof(recvbuf), 0)) > 0) {
        string message(recvbuf, iResult);
        cout << "[LoquiServer] Recibido: " << message << std::endl;

        vector<std::string> parts = split(message, '|');
        if (parts.empty()) continue;

        string cmd = parts[0];
        string response;

        // --- Procesamiento del Protocolo (RF-1.0 a RF-6.0) ---

        if (cmd == "REGISTER" && parts.size() == 3) {
            // RF-1.0: REGISTRO (CON HASHING Y PERSISTENCIA)
            string user = parts[1];
            string pass_plain = parts[2];

            lock_guard<mutex> lock(g_userStoreMutex);
            if (g_userStore.find(user) == g_userStore.end()) {
                // 1. Generar Salt
                string salt = generateSalt();
                // 2. Calcular Hash
                string hash = picosha2::hash256_hex_string(pass_plain + salt);

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
            string user = parts[1];
            string pass_plain = parts[2];

            bool authSuccess = false;
            {
                lock_guard<std::mutex> lock(g_userStoreMutex);
                auto it = g_userStore.find(user);
                if (it != g_userStore.end()) {
                    // Usuario encontrado, verificar contraseña
                    string storedSalt = it->second.salt;
                    string storedHash = it->second.hash;

                    // 1. Calcular hash del intento
                    string attemptHash = picosha2::hash256_hex_string(pass_plain + storedSalt);

                    // 2. Comparar
                    if (attemptHash == storedHash) {
                        authSuccess = true;
                    }
                }
                // Si el usuario no existe (it == g_userStore.end()), authSuccess sigue false
            }

            if (authSuccess) {
                lock_guard<std::mutex> lock(g_clientsMutex);
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
            string toUser = parts[1];
            string chatMessage = parts[2];
            // Reconstruir el mensaje si tenía '|'
            for (size_t i = 3; i < parts.size(); ++i) {
                chatMessage += "|" + parts[i];
            }

            sendMessageToClient(currentUsername, toUser, chatMessage);

        } else if (cmd == "LIST" && !currentUsername.empty()) {
            // RF-5.0: LISTADO DE USUARIOS
            response = "LIST_RESP";
            lock_guard<std::mutex> lock(g_clientsMutex);
            for (auto const& [user, sock] : g_connectedClients) {
                response += "|" + user;
            }
            sendResponse(clientSocket, response); // USAR NUEVO HELPER

        } else if (cmd == "HISTORY" && parts.size() == 2 && !currentUsername.empty()) {
            // NUEVO: RF-7.0 (IMPLÍCITO): SOLICITAR HISTORIAL DE CONVERSACIÓN
            string otherUser = parts[1];
            sendHistoryToClient(clientSocket, currentUsername, otherUser);

        } else if (cmd == "DC") {
            // RF-6.0: CIERRE DE SESIÓN
            break; // Salir del bucle
        }

    } // Fin del bucle while(recv)

    // --- Desconexión del Cliente ---
    cout << "[LoquiServer] Cliente desconectado." << endl;
    if (!currentUsername.empty()) {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_connectedClients.erase(currentUsername);
        cout << "[LoquiServer] Usuario " << currentUsername << " ha cerrado sesion." << endl;
    }
    closesocket(clientSocket);
}

// NUEVA: Agrega el delimitador de fin de mensaje y lo envía
void sendResponse(SOCKET clientSocket, const std::string& response) {
    // Añadimos un delimitador de nueva línea para indicar el final del mensaje
    string delimitedResponse = response + "\n";
    send(clientSocket, delimitedResponse.c_str(), delimitedResponse.length(), 0);
}

// Función auxiliar para enviar un mensaje a un usuario específico
void sendMessageToClient(const std::string& fromUser, const std::string& toUser, const std::string& chatMessage) {
    string timestamp = getCurrentTimestamp();
    // Formato: "MSG|timestamp|fromUser|chatMessage"
    string fullMessage = "MSG|" + timestamp + "|" + fromUser + "|" + chatMessage;

    // 1. Persistir el mensaje
    saveMessage(fromUser, toUser, timestamp, chatMessage);

    // 2. Intentar enviar al destinatario
    SOCKET targetSocket = INVALID_SOCKET;
    {
        lock_guard<std::mutex> lock(g_clientsMutex);
        auto it = g_connectedClients.find(toUser);
        if (it != g_connectedClients.end()) {
            targetSocket = it->second;
        }
    }

    if (targetSocket != INVALID_SOCKET) {
        sendResponse(targetSocket, fullMessage); // USAR NUEVO HELPER
        cout << "[LoquiServer] Enviando " << fullMessage << " a " << toUser << std::endl;
    } else {
        cout << "[LoquiServer] Usuario " << toUser << " no conectado. Mensaje guardado." << std::endl;
        // Opcional: enviar un "RESP|ERROR|Usuario no conectado" al remitente
    }
}

// Función auxiliar para dividir strings
vector<std::string> split(const std::string& s, char delimiter) {
    vector<std::string> tokens;
    string token;
    istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Genera un 'salt' aleatorio de longitud 'length'
string generateSalt(int length) {
    const string CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    random_device random_device;
    mt19937 generator(random_device());
    uniform_int_distribution<> distribution(0, CHARACTERS.length() - 1);

    string salt = "";
    for(int i = 0; i < length; ++i) {
        salt += CHARACTERS[distribution(generator)];
    }
    return salt;
}

// Guarda un nuevo usuario en el archivo CSV (modo append)
void saveUser(const string& username, const UserData& data) {
    // No necesitamos mutex aquí si solo la llamamos desde
    // 'handleClient' DENTRO de un 'lock' existente.
    ofstream file(USER_FILE, std::ios::app); // app = append
    if (file.is_open()) {
        file << username << "," << data.salt << "," << data.hash << "\n";
        file.close();
    } else {
        cerr << "[LoquiServer] ERROR: No se pudo abrir " << USER_FILE << " para escritura." << std::endl;
    }
}

// Carga todos los usuarios desde el archivo CSV al inicio
void loadUsers() {
    ifstream file(USER_FILE);
    if (!file.is_open()) {
        cout << "[LoquiServer] No se encontro " << USER_FILE << ". Se creara uno nuevo al primer registro." << std::endl;
        return;
    }

    string line;
    int count = 0;

    lock_guard<::mutex> lock(g_userStoreMutex);
    while (std::getline(file, line)) {
        std::vector<std::string> parts = split(line, ',');
        if (parts.size() == 3) {
            // Formato: username,salt,hash
            g_userStore[parts[0]] = {parts[1], parts[2]};
            count++;
        }
    }

    file.close();
    cout << "[LoquiServer] Cargados " << count << " usuarios desde " << USER_FILE << "." << endl;
}

// NUEVA: Genera una marca de tiempo en formato YYYY-MM-DD HH:MM:SS
string getCurrentTimestamp() {
    using namespace chrono;
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

    stringstream ss;
    ss << put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// NUEVA: Guarda un mensaje en el archivo de historial
// Formato: timestamp,sender,receiver,"message"
void saveMessage(const std::string& sender, const std::string& receiver, const std::string& timestamp, const std::string& message) {
    // Nota: Aunque el servidor es multithread, ofstream/fstream manejan
    // la exclusión mutua a nivel de sistema operativo para writes a archivos.
    // Usaremos un mutex simple para ser más explícitos si fuera necesario,
    // pero para este simple append es generalmente seguro.
    ofstream file(HISTORY_FILE, std::ios::app);
    if (file.is_open()) {
        // Encerramos el mensaje en comillas dobles para que los delimitadores internos (',') no rompan el CSV
        file << timestamp << "," << sender << "," << receiver << ",\"" << message << "\"\n";
        file.close();
    } else {
        cerr << "[LoquiServer] ERROR: No se pudo abrir " << HISTORY_FILE << " para escritura." << std::endl;
    }
}

// NUEVA: Envía el historial de mensajes entre dos usuarios al cliente
void sendHistoryToClient(SOCKET clientSocket, const std::string& currentUser, const std::string& otherUser) {
    ifstream file(HISTORY_FILE);
    if (!file.is_open()) {
        std::string resp = "RESP|OK|No hay historial de mensajes.";
        sendResponse(clientSocket, resp); // USAR NUEVO HELPER
        return;
    }

    string historyResponse = "HISTORY_RESP|" + otherUser; // HISTORY_RESP|otherUser|
    string line;
    int count = 0;

    while (getline(file, line)) {
        // El formato es: timestamp,sender,receiver,"message"
        // Como el mensaje puede contener comas, usamos una lógica simple
        // para extraer las primeras 3 partes y el resto como mensaje.

        stringstream ss(line);
        string part;
        vector<string> parts;

        // Extraer timestamp
        if (getline(ss, part, ',')) parts.push_back(part);
        // Extraer sender
        if (getline(ss, part, ',')) parts.push_back(part);
        // Extraer receiver
        if (getline(ss, part, ',')) parts.push_back(part);

        // El resto de la línea es el mensaje (que empieza con una comilla doble)
        string messageContent;
        if (getline(ss, messageContent)) {
            // Eliminar las comillas dobles si existen
            if (!messageContent.empty() && messageContent.front() == '"' && messageContent.back() == '"') {
                 messageContent = messageContent.substr(1, messageContent.length() - 2);
            }
            parts.push_back(messageContent);
        }

        if (parts.size() == 4) {
            string timestamp = parts[0];
            string sender = parts[1];
            string receiver = parts[2];
            string message = parts[3];

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
        string resp = "RESP|OK|No hay historial de mensajes con " + otherUser + ".";
        sendResponse(clientSocket, resp); // USAR NUEVO HELPER
    }

    cout << "[LoquiServer] Enviado historial con " << count << " mensajes para " << currentUser << " con " << otherUser << "." << std::endl;
}
