#include <stdio.h>
#include <string.h>
#include <libwebsockets.h>
#include <stdlib.h>
#include <json.h>

// Estructura para manejar las conexiones WebSocket
struct per_session_data__chat {
    char *username;
};

// Callback de WebSocket
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    struct per_session_data__chat *data = (struct per_session_data__chat *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Conexión establecida\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            // Procesamos el mensaje recibido
            char *msg = (char *)in;
            printf("Mensaje recibido: %s\n", msg);
            
            // Parseamos el JSON recibido
            struct json_object *parsed_json;
            parsed_json = json_tokener_parse(msg);
            if (parsed_json != NULL) {
                struct json_object *type_obj, *sender_obj;
                json_object_object_get_ex(parsed_json, "type", &type_obj);
                json_object_object_get_ex(parsed_json, "sender", &sender_obj);
                
                // Si es un mensaje de tipo "register", guardamos el nombre de usuario
                if (json_object_get_string(type_obj) != NULL && strcmp(json_object_get_string(type_obj), "register") == 0) {
                    data->username = strdup(json_object_get_string(sender_obj));
                    printf("Nuevo usuario registrado: %s\n", data->username);
                }
                
                // Respondemos al cliente con un mensaje
                const char *response = "{\"type\": \"register_success\", \"content\": \"Registro exitoso\"}";
                lws_write(wsi, (unsigned char *)response, strlen(response), LWS_WRITE_TEXT);
                json_object_put(parsed_json);
            }
            break;
        }

        case LWS_CALLBACK_CLOSED:
            printf("Conexión cerrada\n");
            break;

        default:
            break;
    }

    return 0;
}

// Configuración de WebSocket y servidor
int main() {
    // Estructura de protocolos
    struct lws_protocols protocols[] = {
        {
            "chat_protocol",   // Nombre del protocolo
            callback_chat,     // Función de callback
            sizeof(struct per_session_data__chat),  // Tamaño de los datos por sesión
            1024,              // Tamaño del buffer de mensaje
        },
        { NULL, NULL, 0, 0 } // Fin de la lista de protocolos
    };

    // Estructura de configuración de contexto de WebSocket
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8080;  // Puerto en el que escuchará el servidor
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    // Crear el contexto WebSocket
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Fallo al crear el contexto de WebSocket\n");
        return -1;
    }

    // Ejecutar el servidor WebSocket
    printf("Servidor WebSocket en ejecución en el puerto 8080...\n");
    while (1) {
        lws_service(context, 1000);  // Procesa los eventos WebSocket cada 1000ms
    }

    // Liberar el contexto cuando se termine
    lws_context_destroy(context);
    return 0;
}
