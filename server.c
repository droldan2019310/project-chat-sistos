#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>
#include <json-c/json.h>

// Espacio máximo para el mensaje, sumado al margen requerido por LWS_PRE
#define MAX_PAYLOAD_SIZE 1024

// Estructura para manejar las conexiones WebSocket por sesión
struct per_session_data__chat {
    char *username;
};

// Callback de WebSocket
static int
callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
              void *user, void *in, size_t len)
{
    struct per_session_data__chat *pss =
        (struct per_session_data__chat *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        printf("Conexión establecida\n");
        // Inicializamos el campo en NULL, por si la sesión lo usa más adelante
        pss->username = NULL;
        break;

    case LWS_CALLBACK_RECEIVE:
        if (!in || len == 0) {
            printf("Error: Datos nulos o longitud cero.\n");
            break;
        } else {
            // Mostramos el mensaje recibido
            printf("Mensaje recibido: %.*s\n", (int)len, (char *)in);

            // Parseamos el JSON recibido
            struct json_object *parsed_json = json_tokener_parse((char *)in);
            if (parsed_json) {
                struct json_object *type_obj = NULL, *sender_obj = NULL;

                // Extraemos campos "type" y "sender"
                json_object_object_get_ex(parsed_json, "type", &type_obj);
                json_object_object_get_ex(parsed_json, "sender", &sender_obj);

                // Verificamos que existan y sean cadenas
                if (type_obj && sender_obj) {
                    const char *type_str = json_object_get_string(type_obj);
                    const char *sender_str = json_object_get_string(sender_obj);

                    if (type_str && sender_str &&
                        strcmp(type_str, "register") == 0) {
                        // Liberar si ya está asignado
                        if (pss->username) {
                            free(pss->username);
                            pss->username = NULL;
                        }
                        pss->username = strdup(sender_str);
                        printf("Nuevo usuario registrado: %s\n", pss->username);
                    }
                }

                // Responder al cliente con un mensaje
                const char *response =
                    "{\"type\": \"register_success\", \"content\": \"Registro exitoso\"}";

                // Usar un buffer con LWS_PRE para lws_write
                unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
                memset(buffer, 0, sizeof(buffer));

                size_t resp_len = strlen(response);
                // Copiamos la respuesta a partir de LWS_PRE
                memcpy(&buffer[LWS_PRE], response, resp_len);

                // Escribimos la respuesta usando lws_write
                int n = lws_write(wsi, &buffer[LWS_PRE], resp_len, LWS_WRITE_TEXT);
                if (n < (int)resp_len) {
                    printf("Error al enviar la respuesta al cliente\n");
                }

                json_object_put(parsed_json);
            } else {
                printf("Error: El mensaje recibido no es JSON válido.\n");
            }
        }
        break;

    case LWS_CALLBACK_CLOSED:
        printf("Conexión cerrada\n");
        // Liberamos la memoria que asignamos para el username
        if (pss->username) {
            free(pss->username);
            pss->username = NULL;
        }
        break;

    default:
        break;
    }

    return 0;
}

int main(void)
{
    // Definimos el protocolo que manejará nuestro servidor
    struct lws_protocols protocols[] = {
        {
            "chat_protocol",                // Nombre del protocolo
            callback_chat,                  // Función de callback
            sizeof(struct per_session_data__chat),  // Tamaño de la estructura por sesión
            MAX_PAYLOAD_SIZE,              // Tamaño máximo del buffer por sesión
        },
        { NULL, NULL, 0, 0 } // Fin de la lista de protocolos
    };

    // Estructura de configuración del contexto
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = 8080;           // Puerto en el que escuchará el servidor
    info.protocols = protocols;  // Lista de protocolos
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
        // Procesa los eventos WebSocket cada 1000ms
        lws_service(context, 1000);
    }

    // Liberar el contexto cuando se termine
    lws_context_destroy(context);
    return 0;
}
