#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>
#include <json-c/json.h>

#define MAX_PAYLOAD_SIZE 1024

// Estructura para la sesión del usuario
struct per_session_data__chat {
    char *username;
    struct lws *wsi;
};

// Lista global de conexiones activas
#define MAX_CLIENTES 100
static struct per_session_data__chat *clientes[MAX_CLIENTES] = { NULL };

// Añadir cliente a la lista
void registrar_cliente(struct per_session_data__chat *pss) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i] == NULL) {
            clientes[i] = pss;
            break;
        }
    }
}

// Remover cliente de la lista
void eliminar_cliente(struct per_session_data__chat *pss) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i] == pss) {
            clientes[i] = NULL;
            break;
        }
    }
}

// Buscar cliente por nombre de usuario
struct per_session_data__chat *buscar_destinatario(const char *nombre) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i] && clientes[i]->username && strcmp(clientes[i]->username, nombre) == 0) {
            return clientes[i];
        }
    }
    return NULL;
}

// Enviar JSON a un cliente
void enviar_a_cliente(struct lws *wsi, const char *json_msg) {
    unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
    memset(buffer, 0, sizeof(buffer));
    size_t len = strlen(json_msg);
    memcpy(&buffer[LWS_PRE], json_msg, len);
    lws_write(wsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
}

// Callback principal
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    struct per_session_data__chat *pss = (struct per_session_data__chat *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        printf("Conexión establecida\n");
        pss->username = NULL;
        pss->wsi = wsi;
        registrar_cliente(pss);
        break;

    case LWS_CALLBACK_RECEIVE:
        if (!in || len == 0) break;

        printf("Mensaje recibido: %.*s\n", (int)len, (char *)in);
        struct json_object *parsed = json_tokener_parse((char *)in);
        if (!parsed) break;

        struct json_object *type, *sender, *content, *target;
        json_object_object_get_ex(parsed, "type", &type);
        json_object_object_get_ex(parsed, "sender", &sender);
        json_object_object_get_ex(parsed, "content", &content);
        json_object_object_get_ex(parsed, "target", &target);

        const char *type_str = json_object_get_string(type);
        const char *sender_str = json_object_get_string(sender);
        const char *content_str = json_object_get_string(content);

        if (strcmp(type_str, "register") == 0 && sender_str) {
            if (pss->username) free(pss->username);
            pss->username = strdup(sender_str);
            printf("Usuario registrado: %s\n", pss->username);

            const char *resp = "{\"type\": \"register_success\", \"content\": \"Registro exitoso\"}";
            enviar_a_cliente(wsi, resp);
        }
        else if (strcmp(type_str, "chat") == 0 && sender_str && content_str) {
            char broadcast[MAX_PAYLOAD_SIZE];
            snprintf(broadcast, sizeof(broadcast),
                     "{\"type\":\"chat\",\"sender\":\"%s\",\"content\":\"%s\"}",
                     sender_str, content_str);

            for (int i = 0; i < MAX_CLIENTES; i++) {
                if (clientes[i] && clientes[i]->wsi != wsi) {
                    enviar_a_cliente(clientes[i]->wsi, broadcast);
                }
            }
        }
        else if (strcmp(type_str, "private") == 0 && sender_str && content_str && target) {
            const char *target_str = json_object_get_string(target);
            struct per_session_data__chat *dest = buscar_destinatario(target_str);

            if (dest && dest->wsi) {
                char privado[MAX_PAYLOAD_SIZE];
                snprintf(privado, sizeof(privado),
                         "{\"type\":\"private\",\"sender\":\"%s\",\"content\":\"%s\"}",
                         sender_str, content_str);
                enviar_a_cliente(dest->wsi, privado);
            } else {
                printf("Usuario destino no encontrado: %s\n", target_str);
            }
        }

        json_object_put(parsed);
        break;

    case LWS_CALLBACK_CLOSED:
        printf("Conexión cerrada\n");
        eliminar_cliente(pss);
        if (pss->username) free(pss->username);
        pss->username = NULL;
        break;

    default:
        break;
    }

    return 0;
}

// Main
int main(void)
{
    struct lws_protocols protocols[] = {
        {
            "chat_protocol",
            callback_chat,
            sizeof(struct per_session_data__chat),
            MAX_PAYLOAD_SIZE,
        },
        { NULL, NULL, 0, 0 }
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Fallo al crear el contexto WebSocket\n");
        return -1;
    }

    printf("Servidor WebSocket en ejecución en el puerto 8080...\n");
    while (1) {
        lws_service(context, 1000);
    }

    lws_context_destroy(context);
    return 0;
}
