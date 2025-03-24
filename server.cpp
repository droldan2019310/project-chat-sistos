#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <libwebsockets.h>
#include <json/json.h>

// Estructura para manejar las conexiones WebSocket
struct connection_info {
    struct lws *wsi;
    std::string username;
    std::string status;
};

// Estructura global para mantener las conexiones activas
std::vector<connection_info> connections;

// Función de callback de WebSocket
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    connection_info *ci = (connection_info *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        std::cout << "Nueva conexión establecida." << std::endl;
        break;

    case LWS_CALLBACK_RECEIVE:
        // Procesar el mensaje recibido
        {
            std::string message((char *)in, len);
            std::cout << "Mensaje recibido: " << message << std::endl;

            // Aquí deberías agregar el procesamiento del mensaje en JSON
            Json::Reader reader;
            Json::Value root;
            if (reader.parse(message, root)) {
                std::string type = root["type"].asString();
                if (type == "register") {
                    ci->username = root["sender"].asString();
                    std::cout << "Usuario registrado: " << ci->username << std::endl;
                }
                // Procesar otros tipos de mensajes (broadcast, private, etc.)
            }

            // Responder al cliente
            Json::Value response;
            response["type"] = "register_success";
            response["sender"] = "server";
            response["content"] = "Registro exitoso";
            response["timestamp"] = "2025-03-10T12:00:00"; // Aquí puedes poner la fecha actual.

            Json::StreamWriterBuilder writer;
            std::string jsonResponse = Json::writeString(writer, response);
            lws_write(wsi, (unsigned char *)jsonResponse.c_str(), jsonResponse.length(), LWS_WRITE_TEXT);
        }
        break;

    case LWS_CALLBACK_CLOSED:
        std::cout << "Conexión cerrada." << std::endl;
        break;

    default:
        break;
    }

    return 0;
}

// Configuración de WebSocket y servidor
int main() {
    // Inicializar las opciones de WebSocket
    struct lws_context_creation_info info;
    struct lws_protocols protocols[] = {
        {
            "chat_protocol", callback_chat, sizeof(connection_info), 1024
        },
        { NULL, NULL, 0, 0 } // Fin de la lista de protocolos
    };

    memset(&info, 0, sizeof(info));
    info.port = 8080; // Puerto del servidor WebSocket
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    // Crear el contexto WebSocket
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        std::cerr << "Fallo la creación del contexto de WebSocket." << std::endl;
        return -1;
    }

    // Ejecutar el servidor WebSocket
    while (true) {
        lws_service(context, 1000); // Procesa eventos WebSocket cada 1000ms
    }

    // Liberar el contexto cuando se termine
    lws_context_destroy(context);
    return 0;
}
