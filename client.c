#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwebsockets.h>
#include <unistd.h> // Para usleep (opcional)

// Buffer con espacio para encabezados WebSocket
#define MAX_PAYLOAD_SIZE 1024

// Estructura para datos por sesión (expandir si requieres más)
struct per_session_data__chat {
    char *username;  
};

// Variables globales para simplicidad (puedes encapsularlas en otro contexto)
static struct lws *global_wsi = NULL;       // Guardará la conexión WebSocket
static int g_connection_established = 0;    // Indica si la conexión se estableció
static char g_username[100] = "invitado";   // Nombre de usuario por defecto

//------------------------------------------------------------------------------
// Función auxiliar para enviar un mensaje JSON al servidor
//------------------------------------------------------------------------------
static int
send_json_message(struct lws *wsi, const char *type, const char *sender, const char *content)
{
    if (!wsi) {
        fprintf(stderr, "[send_json_message] Error: wsi es NULL (conexión no establecida)\n");
        return -1;
    }

    // Buffer con espacio para LWS_PRE
    unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Apuntamos a la parte del buffer donde inicia nuestro JSON
    char *json_part = (char *)&buffer[LWS_PRE];

    // Construimos el JSON
    // Ejemplo: {"type":"chat","sender":"nombre","content":"hola"}
    snprintf(json_part, MAX_PAYLOAD_SIZE,
             "{\"type\":\"%s\",\"sender\":\"%s\",\"content\":\"%s\"}",
             type, sender, content);

    size_t msg_len = strlen(json_part);

    // Enviamos
    int written = lws_write(wsi,
                            (unsigned char *)json_part,
                            msg_len,
                            LWS_WRITE_TEXT);

    if (written < (int)msg_len) {
        fprintf(stderr, "[send_json_message] Error al enviar mensaje. Retorno: %d\n", written);
        return -1;
    }

    printf("[send_json_message] Mensaje enviado: %s\n", json_part);
    return 0;
}

//------------------------------------------------------------------------------
// Callback de WebSocket
//------------------------------------------------------------------------------
static int
callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
              void *user, void *in, size_t len)
{
    struct per_session_data__chat *pss = (struct per_session_data__chat *)user;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        printf("[callback_chat] Conexión establecida con el servidor WebSocket\n");
        global_wsi = wsi;
        g_connection_established = 1;  // Marcamos que la conexión está lista
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (in && len > 0) {
            printf("[callback_chat] Mensaje recibido (%zu bytes): %.*s\n",
                   len, (int)len, (char *)in);
        } else {
            printf("[callback_chat] Error: No se recibieron datos o longitud cero\n");
        }
        break;

    case LWS_CALLBACK_CLOSED:
        printf("[callback_chat] Conexión cerrada\n");
        global_wsi = NULL;
        g_connection_established = 0;

        // Liberar recursos de la sesión si lo usaste
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

//------------------------------------------------------------------------------
// Función para mostrar el menú y procesar la elección del usuario
//------------------------------------------------------------------------------
static void
menu_interactivo(struct lws_context *context)
{
    int opcion = 0;
    char mensaje[256];

    while (1) {
        // Procesamos eventos brevemente (no bloqueante)
        // para dar tiempo a libwebsockets de manejar mensajes
        lws_service(context, 50);

        printf("\n=== MENÚ ===\n");
        printf("1) Registrar usuario\n");
        printf("2) Enviar mensaje\n");
        printf("3) Salir\n");
        printf("Selecciona una opción: ");
        fflush(stdout);

        // Leer opción
        if (scanf("%d", &opcion) != 1) {
            fprintf(stderr, "Entrada inválida. Intenta de nuevo.\n");
            // Limpiar el buffer de entrada
            fseek(stdin, 0, SEEK_END);
            continue;
        }

        switch (opcion) {
        case 1: { // Registrar usuario
            printf("Ingresa el nombre de usuario: ");
            scanf("%s", g_username);

            // Si la conexión no está lista, avisamos
            if (!g_connection_established || !global_wsi) {
                printf("Aún no se ha establecido la conexión. Intenta más tarde.\n");
                break;
            }

            // Enviamos el mensaje de "register"
            send_json_message(global_wsi, "register", g_username, "Registro de usuario");
            break;
        }
        case 2: { // Enviar mensaje de chat
            // Consumimos salto de línea pendiente, si lo hubiera
            fgetc(stdin);

            printf("Ingresa el mensaje a enviar: ");
            if (fgets(mensaje, sizeof(mensaje), stdin) == NULL) {
                fprintf(stderr, "Error al leer el mensaje.\n");
                break;
            }
            // Quitar el salto de línea
            mensaje[strcspn(mensaje, "\n")] = 0;

            // Verificar conexión
            if (!g_connection_established || !global_wsi) {
                printf("Aún no se ha establecido la conexión. Intenta más tarde.\n");
                break;
            }

            // Enviamos el mensaje JSON
            send_json_message(global_wsi, "chat", g_username, mensaje);
            break;
        }
        case 3:
            printf("Saliendo...\n");
            return;
        default:
            printf("Opción inválida. Intenta de nuevo.\n");
            break;
        }
    }
}

int main()
{
    // Ajustar nivel de logging si quieres más o menos detalle
    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, NULL);

    // Crear la info para el contexto
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN; // Cliente no escucha
    static struct lws_protocols protocols[] = {
        {
            "chat_protocol",
            callback_chat,
            sizeof(struct per_session_data__chat),
            MAX_PAYLOAD_SIZE
        },
        { NULL, NULL, 0, 0 }
    };
    info.protocols = protocols;

    // Creamos el contexto
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "[main] Error al crear el contexto de libwebsockets\n");
        return -1;
    }

    // Información para la conexión
    struct lws_client_connect_info ccinfo = {
        .context = context,
        .address = "localhost", // Ajusta si el servidor está en otra IP
        .port = 8080,
        .path = "/chat",
        .host = "localhost",
        .origin = "localhost",
        .protocol = "chat_protocol",
        .ssl_connection = 0
    };

    // Conexión
    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        fprintf(stderr, "[main] Fallo al conectar con el servidor WebSocket\n");
        lws_context_destroy(context);
        return -1;
    }

    printf("[main] Conectando al servidor...\n");

    // Esperar hasta que la conexión se establezca antes de mostrar el menú
    // (enfoque bloqueante, pero simple)
    while (!g_connection_established) {
        lws_service(context, 50);
        // Pequeña pausa para no saturar la CPU
        usleep(50000); // 50 ms
    }

    printf("[main] Conexión establecida. Ahora vamos al menú.\n");

    // Lanzamos el menú interactivo
    menu_interactivo(context);

    // Al salir del menú, cerramos ordenadamente
    lws_context_destroy(context);
    return 0;
}
