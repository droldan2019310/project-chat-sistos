#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libwebsockets.h>
#include <json-c/json.h>

#define MAX_PAYLOAD_SIZE 1024
#define MAX_CLIENTES 100

// Estados posibles: "ACTIVO", "OCUPADO", "INACTIVO"
enum estado_usuario {
    ESTADO_ACTIVO,
    ESTADO_OCUPADO,
    ESTADO_INACTIVO
};

struct per_session_data__chat {
    char *username;           // Nombre de usuario
    char ip[64];              // IP del cliente
    enum estado_usuario est;  // Estado (ACTIVO, OCUPADO, INACTIVO)
    struct lws *wsi;
};

// Lista global de conexiones
static struct per_session_data__chat *clientes[MAX_CLIENTES] = { NULL };

//------------------------------------------------------------------------------
// Función para tomar la hora actual en ISO8601 (aproximado) o un formato de tu elección
//------------------------------------------------------------------------------
static void get_timestamp(char *buf, size_t buflen) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", tm_info);
}

//------------------------------------------------------------------------------
// Añadir cliente a la lista
//------------------------------------------------------------------------------
void registrar_cliente(struct per_session_data__chat *pss) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i] == NULL) {
            clientes[i] = pss;
            break;
        }
    }
}

//------------------------------------------------------------------------------
// Remover cliente de la lista
//------------------------------------------------------------------------------
void eliminar_cliente(struct per_session_data__chat *pss) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i] == pss) {
            clientes[i] = NULL;
            break;
        }
    }
}

//------------------------------------------------------------------------------
// Buscar un cliente por nombre
//------------------------------------------------------------------------------
struct per_session_data__chat *buscar_destinatario(const char *nombre) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (clientes[i]
            && clientes[i]->username
            && strcmp(clientes[i]->username, nombre) == 0) {
            return clientes[i];
        }
    }
    return NULL;
}

//------------------------------------------------------------------------------
// Convertir enum estado_usuario a string
//------------------------------------------------------------------------------
static const char* estado_to_string(enum estado_usuario e) {
    switch(e) {
        case ESTADO_ACTIVO:   return "ACTIVO";
        case ESTADO_OCUPADO:  return "OCUPADO";
        case ESTADO_INACTIVO: return "INACTIVO";
        default: return "ACTIVO";
    }
}

//------------------------------------------------------------------------------
// Enviar un JSON (string) a un cliente
//------------------------------------------------------------------------------
static void enviar_a_cliente(struct lws *wsi, const char *json_msg) {
    unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
    memset(buffer, 0, sizeof(buffer));
    size_t len = strlen(json_msg);
    memcpy(&buffer[LWS_PRE], json_msg, len);
    lws_write(wsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
}

//------------------------------------------------------------------------------
// Callback principal
//------------------------------------------------------------------------------
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    struct per_session_data__chat *pss = (struct per_session_data__chat *)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        printf("Conexión establecida\n");
        // Iniciar la estructura
        pss->username = NULL;
        pss->ip[0] = '\0';
        pss->est = ESTADO_ACTIVO;
        pss->wsi = wsi;

        // Extraer la IP del cliente (si la versión de libwebsockets lo soporta)
        char ip_buf[64];
        if (lws_get_peer_simple(wsi, ip_buf, sizeof(ip_buf))) {
            strncpy(pss->ip, ip_buf, sizeof(pss->ip) - 1);
            pss->ip[sizeof(pss->ip)-1] = '\0';
            printf("IP del cliente: %s\n", pss->ip);
        }

        registrar_cliente(pss);
        break;
    }

    case LWS_CALLBACK_RECEIVE:
        if (!in || len == 0) break;

        printf("Mensaje recibido: %.*s\n", (int)len, (char *)in);
        {
            // Parsear el JSON
            struct json_object *parsed = json_tokener_parse((char *)in);
            if (!parsed) break;

            // Extraer campos: type, sender, target, content, timestamp
            struct json_object *jtype, *jsender, *jtarget, *jcontent, *jtstamp;
            json_object_object_get_ex(parsed, "type", &jtype);
            json_object_object_get_ex(parsed, "sender", &jsender);
            json_object_object_get_ex(parsed, "target", &jtarget);
            json_object_object_get_ex(parsed, "content", &jcontent);
            json_object_object_get_ex(parsed, "timestamp", &jtstamp);

            const char *type_str     = jtype    ? json_object_get_string(jtype)    : NULL;
            const char *sender_str   = jsender  ? json_object_get_string(jsender)  : NULL;
            const char *target_str   = jtarget  ? json_object_get_string(jtarget)  : NULL;
            const char *content_str  = jcontent ? json_object_get_string(jcontent) : NULL;

            // Para la respuesta del servidor
            char out_ts[64];
            get_timestamp(out_ts, sizeof(out_ts));

            if (type_str && strcmp(type_str, "register") == 0) {
                // El cliente manda {type:"register",sender:"<user>",content:null}
                if (pss->username) free(pss->username);
                pss->username = strdup(sender_str ? sender_str : "anon");
                pss->est = ESTADO_ACTIVO;

                printf("Usuario registrado: %s\n", pss->username);

                // Respuesta "register_success" con userList (opcional)
                struct json_object *jresp = json_object_new_object();
                json_object_object_add(jresp, "type",
                    json_object_new_string("register_success"));
                json_object_object_add(jresp, "sender",
                    json_object_new_string("server"));
                json_object_object_add(jresp, "content",
                    json_object_new_string("Registro exitoso"));
                // Armar un array con la lista de usuarios
                struct json_object *jarr = json_object_new_array();
                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (clientes[i] && clientes[i]->username) {
                        json_object_array_add(jarr,
                            json_object_new_string(clientes[i]->username));
                    }
                }
                json_object_object_add(jresp, "userList", jarr);
                json_object_object_add(jresp, "timestamp",
                    json_object_new_string(out_ts));

                const char *resp_str = json_object_to_json_string(jresp);
                enviar_a_cliente(wsi, resp_str);
                json_object_put(jresp);
            }
            else if (type_str && strcmp(type_str, "broadcast") == 0) {
                // Mensaje general a todos
                // {type:"broadcast", sender:"...", content:"...", timestamp:"..."}

                // Armar JSON de broadcast
                struct json_object *jresp = json_object_new_object();
                json_object_object_add(jresp, "type",
                    json_object_new_string("broadcast"));
                json_object_object_add(jresp, "sender",
                    json_object_new_string(sender_str ? sender_str : "anon"));
                json_object_object_add(jresp, "content",
                    json_object_new_string(content_str ? content_str : ""));
                json_object_object_add(jresp, "timestamp",
                    json_object_new_string(out_ts));

                const char *broad_str = json_object_to_json_string(jresp);

                // Enviar a todos menos al emisor
                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (clientes[i] && clientes[i]->wsi != wsi) {
                        enviar_a_cliente(clientes[i]->wsi, broad_str);
                    }
                }
                json_object_put(jresp);
            }
            else if (type_str && strcmp(type_str, "private") == 0) {
                // {type:"private", sender:"...", target:"...", content:"...", timestamp:"..."}
                if (!target_str) {
                    // No se dio target
                    // Podríamos mandar un error
                    break;
                }
                struct per_session_data__chat *dest = buscar_destinatario(target_str);
                if (dest && dest->wsi) {
                    // Armar JSON
                    struct json_object *jresp = json_object_new_object();
                    json_object_object_add(jresp, "type",
                        json_object_new_string("private"));
                    json_object_object_add(jresp, "sender",
                        json_object_new_string(sender_str ? sender_str : "anon"));
                    json_object_object_add(jresp, "content",
                        json_object_new_string(content_str ? content_str : ""));
                    json_object_object_add(jresp, "timestamp",
                        json_object_new_string(out_ts));

                    const char *priv_str = json_object_to_json_string(jresp);
                    enviar_a_cliente(dest->wsi, priv_str);
                    json_object_put(jresp);
                }
                else {
                    printf("Usuario destino no encontrado: %s\n", target_str);
                    // Podrías mandar un mensaje de error al emisor
                }
            }
            else if (type_str && strcmp(type_str, "list_users") == 0) {
                // {type:"list_users", sender:"..."}
                struct json_object *jresp = json_object_new_object();
                json_object_object_add(jresp, "type",
                    json_object_new_string("list_users_response"));
                json_object_object_add(jresp, "sender",
                    json_object_new_string("server"));

                // Construir un array con {username, status}
                struct json_object *jarr = json_object_new_array();
                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (clientes[i] && clientes[i]->username) {
                        struct json_object *juser = json_object_new_object();
                        json_object_object_add(juser, "username",
                            json_object_new_string(clientes[i]->username));
                        json_object_object_add(juser, "status",
                            json_object_new_string(estado_to_string(clientes[i]->est)));
                        json_object_array_add(jarr, juser);
                    }
                }
                json_object_object_add(jresp, "content", jarr);
                json_object_object_add(jresp, "timestamp",
                    json_object_new_string(out_ts));

                const char *resp_str = json_object_to_json_string(jresp);
                enviar_a_cliente(wsi, resp_str);
                json_object_put(jresp);
            }
            else if (type_str && strcmp(type_str, "user_info") == 0) {
                // {type:"user_info", sender:"...", target:"usuario_objetivo"}
                if (!target_str) break;
                struct per_session_data__chat *info_usr = buscar_destinatario(target_str);
                if (info_usr) {
                    struct json_object *jresp = json_object_new_object();
                    json_object_object_add(jresp, "type",
                        json_object_new_string("user_info_response"));
                    json_object_object_add(jresp, "sender",
                        json_object_new_string("server"));
                    json_object_object_add(jresp, "target",
                        json_object_new_string(target_str));

                    // content: {"ip":"...", "status":"..."}
                    struct json_object *jcontent = json_object_new_object();
                    json_object_object_add(jcontent, "ip",
                        json_object_new_string(info_usr->ip));
                    json_object_object_add(jcontent, "status",
                        json_object_new_string(estado_to_string(info_usr->est)));

                    json_object_object_add(jresp, "content", jcontent);
                    json_object_object_add(jresp, "timestamp",
                        json_object_new_string(out_ts));

                    const char *res = json_object_to_json_string(jresp);
                    enviar_a_cliente(wsi, res);
                    json_object_put(jresp);
                }
            }
            else if (type_str && strcmp(type_str, "change_status") == 0) {
                // {type:"change_status", sender:"...", content:"<nuevo_estado>"}
                // Ej: content="OCUPADO"
                if (content_str) {
                    // Actualizar pss->est de acuerdo al contenido
                    if (strcmp(content_str, "ACTIVO") == 0) {
                        pss->est = ESTADO_ACTIVO;
                    } else if (strcmp(content_str, "OCUPADO") == 0) {
                        pss->est = ESTADO_OCUPADO;
                    } else {
                        pss->est = ESTADO_INACTIVO;
                    }
                }
                // Responder con "status_update"
                struct json_object *jresp = json_object_new_object();
                json_object_object_add(jresp, "type",
                    json_object_new_string("status_update"));
                json_object_object_add(jresp, "sender",
                    json_object_new_string("server"));

                // content: {"user":..., "status":...}
                struct json_object *jcont = json_object_new_object();
                json_object_object_add(jcont, "user",
                    json_object_new_string(pss->username ? pss->username : "anon"));
                json_object_object_add(jcont, "status",
                    json_object_new_string(estado_to_string(pss->est)));

                json_object_object_add(jresp, "content", jcont);
                json_object_object_add(jresp, "timestamp",
                    json_object_new_string(out_ts));

                const char *resp_str = json_object_to_json_string(jresp);
                enviar_a_cliente(wsi, resp_str);
                json_object_put(jresp);
            }
            else if (type_str && strcmp(type_str, "disconnect") == 0) {
                // {type:"disconnect", sender:"...", content:"Cierre de sesión"}
                // Responder user_disconnected
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "{\"type\":\"user_disconnected\",\"sender\":\"server\","
                         "\"content\":\"%s ha salido\",\"timestamp\":\"%s\"}",
                         pss->username ? pss->username : "anon", out_ts);
                enviar_a_cliente(wsi, msg);

                // Eliminar al usuario
                printf("El usuario %s se desconectó\n", pss->username);
                eliminar_cliente(pss);
                if (pss->username) {
                    free(pss->username);
                    pss->username = NULL;
                }

                // O marcarlo inactivo, pero según el protocolo cierra
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                lws_set_timeout(wsi, NO_PENDING_TIMEOUT, 1);
            }

            json_object_put(parsed);
        }
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

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
int main(void)
{
    // Definimos el protocolo
    struct lws_protocols protocols[] = {
        {
            "chat_protocol",
            callback_chat,
            sizeof(struct per_session_data__chat),
            MAX_PAYLOAD_SIZE,
        },
        { NULL, NULL, 0, 0 }
    };

    // Crear la info para el contexto
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    // Crear el contexto
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
