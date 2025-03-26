/******************************************************************************
 * client_thread.c
 * Ejemplo de cliente WebSocket en C con hilo aparte para lws_service
 *****************************************************************************/

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>          // usleep
 #include <pthread.h>         // hilos
 #include <libwebsockets.h>
 #include <json-c/json.h>     // Manejo de JSON
 
 //-----------------------------------------------------------------------------
 // Configuraciones
 //-----------------------------------------------------------------------------
 #define MAX_PAYLOAD_SIZE 1024
 #define MAX_CHAT_LINES   20
 
 //-----------------------------------------------------------------------------
 // Estructura por sesión
 //-----------------------------------------------------------------------------
 struct per_session_data__chat {
     char *username;
 };
 
 //-----------------------------------------------------------------------------
 // Variables globales
 //-----------------------------------------------------------------------------
 static struct lws *global_wsi = NULL;
 static int g_connection_established = 0;
 static char g_username[100] = "invitado";
 
 // Para almacenar las últimas líneas del chat
 static char chat_log[MAX_CHAT_LINES][256];
 static int  chat_count = 0;
 
 // Hilo de servicio
 static volatile int stop_service = 0;
 static pthread_t service_thread_id;
 static struct lws_context *global_context = NULL;
 
 //-----------------------------------------------------------------------------
 // Funciones auxiliares para el “chat_log”
 //-----------------------------------------------------------------------------
 static void add_chat_line(const char *line) {
     if (chat_count == MAX_CHAT_LINES) {
         // Desplazar líneas hacia arriba
         for (int i = 1; i < MAX_CHAT_LINES; i++) {
             strcpy(chat_log[i-1], chat_log[i]);
         }
         chat_count--;
     }
     strncpy(chat_log[chat_count], line, sizeof(chat_log[0]) - 1);
     chat_log[chat_count][sizeof(chat_log[0]) - 1] = '\0';
     chat_count++;
 }
 
 // Redibuja la interfaz: limpia pantalla, muestra chat y menú
 static void print_interface() {
     // Limpiar terminal (puede causar “parpadeo”)
     system("clear");
 
     // Mostrar cuadro de chat
     printf("========== CHAT LOG (últimas %d líneas) ==========\n", MAX_CHAT_LINES);
     for (int i = 0; i < chat_count; i++) {
         printf("%s\n", chat_log[i]);
     }
     printf("==================================================\n\n");
 
     // Menú
     printf("=== MENÚ ===\n");
     printf("1) Enviar mensaje (broadcast)\n");
     printf("2) Enviar mensaje privado\n");
     printf("3) Salir\n");
     printf("Selecciona una opción: ");
     fflush(stdout);
 }
 
 //-----------------------------------------------------------------------------
 // Hilo que se encarga de llamar lws_service
 //-----------------------------------------------------------------------------
 static void* service_loop(void* arg) {
     struct lws_context *ctx = (struct lws_context *)arg;
     while (!stop_service) {
         lws_service(ctx, 50);
         // Pequeña pausa para no saturar CPU
         usleep(5000);
     }
     return NULL;
 }
 
 //-----------------------------------------------------------------------------
 // Funciones para enviar mensajes
 //-----------------------------------------------------------------------------
 static int send_json_message(struct lws *wsi, const char *type,
                              const char *sender, const char *content)
 {
     if (!wsi) {
         fprintf(stderr, "[send_json_message] Error: wsi es NULL\n");
         return -1;
     }
 
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"%s\",\"sender\":\"%s\",\"content\":\"%s\"}",
              type, sender, content);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_message] Error al enviar. Ret=%d\n", written);
         return -1;
     }
 
     // Mostrarlo en nuestro chat local
     char temp[256];
     snprintf(temp, sizeof(temp), "[Tú->Broadcast] %s", content);
     add_chat_line(temp);
     print_interface(); // Redibujar al momento
 
     return 0;
 }
 
 static int send_json_message_private(struct lws *wsi, const char *sender,
                                      const char *target, const char *content)
 {
     if (!wsi) {
         fprintf(stderr, "[send_json_message_private] Error: wsi es NULL\n");
         return -1;
     }
 
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"private\",\"sender\":\"%s\",\"target\":\"%s\",\"content\":\"%s\"}",
              sender, target, content);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_message_private] Error al enviar (ret=%d)\n", written);
         return -1;
     }
 
     char temp[256];
     snprintf(temp, sizeof(temp), "[Tú->%s] %s", target, content);
     add_chat_line(temp);
     print_interface();
 
     return 0;
 }
 
 //-----------------------------------------------------------------------------
 // Callback para eventos WebSocket
 //-----------------------------------------------------------------------------
 static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len)
 {
     switch (reason) {
     case LWS_CALLBACK_CLIENT_ESTABLISHED:
         add_chat_line("[Sistema] Conexión establecida");
         global_wsi = wsi;
         g_connection_established = 1;
         print_interface();
         break;
 
     case LWS_CALLBACK_CLIENT_RECEIVE:
         if (in && len > 0) {
             // Parsear JSON
             struct json_object *parsed = json_tokener_parse((char *)in);
             if (!parsed) {
                 // Mensaje RAW
                 char raw_line[256];
                 snprintf(raw_line, sizeof(raw_line), "[Recibido RAW]: %.*s", (int)len, (char *)in);
                 add_chat_line(raw_line);
                 print_interface();
                 break;
             }
 
             // Extraer campos
             struct json_object *type, *sender, *content;
             json_object_object_get_ex(parsed, "type", &type);
             json_object_object_get_ex(parsed, "sender", &sender);
             json_object_object_get_ex(parsed, "content", &content);
 
             const char *type_str    = json_object_get_string(type);
             const char *sender_str  = json_object_get_string(sender);
             const char *content_str = json_object_get_string(content);
 
             if (type_str && strcmp(type_str, "chat") == 0) {
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[msg público] %s: %s", sender_str, content_str);
                 add_chat_line(line);
 
             } else if (type_str && strcmp(type_str, "private") == 0) {
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[msg privado] %s te dice: %s", sender_str, content_str);
                 add_chat_line(line);
 
             } else if (type_str && strcmp(type_str, "register_success") == 0) {
                 add_chat_line("[Sistema] Registro exitoso");
             } else {
                 // Otra cosa
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[Recibido] %.*s", (int)len, (char *)in);
                 add_chat_line(line);
             }
 
             json_object_put(parsed);
             // Redibujar
             print_interface();
         } else {
             add_chat_line("[Sistema] Error: mensaje vacío o nulo");
             print_interface();
         }
         break;
 
     case LWS_CALLBACK_CLOSED:
         add_chat_line("[Sistema] Conexión cerrada");
         global_wsi = NULL;
         g_connection_established = 0;
         print_interface();
         break;
 
     default:
         break;
     }
 
     return 0;
 }
 
 //-----------------------------------------------------------------------------
 // Menú interactivo (bloqueante) en el hilo principal
 //-----------------------------------------------------------------------------
 static void menu_interactivo(void) {
     while (1) {
         print_interface();
 
         int opcion = 0;
         if (scanf("%d", &opcion) != 1) {
             fprintf(stderr, "[Sistema] Entrada inválida.\n");
             fseek(stdin, 0, SEEK_END);
             continue;
         }
 
         // Consumir salto de línea
         fgetc(stdin);
 
         if (!g_connection_established || !global_wsi) {
             add_chat_line("[Sistema] Conexión no establecida aún.");
             continue;
         }
 
         switch (opcion) {
         case 1: {
             char mensaje[256];
             printf("Ingresa el mensaje a enviar (broadcast): ");
             if (fgets(mensaje, sizeof(mensaje), stdin) == NULL) {
                 add_chat_line("[Sistema] Error al leer broadcast.");
                 break;
             }
             mensaje[strcspn(mensaje, "\n")] = 0;
             send_json_message(global_wsi, "chat", g_username, mensaje);
             break;
         }
         case 2: {
             char target[100];
             char mensaje[256];
             printf("Usuario destino: ");
             if (scanf("%s", target) != 1) {
                 add_chat_line("[Sistema] Error al leer usuario destino.");
                 fseek(stdin, 0, SEEK_END);
                 break;
             }
             fgetc(stdin);
 
             printf("Mensaje privado: ");
             if (fgets(mensaje, sizeof(mensaje), stdin) == NULL) {
                 add_chat_line("[Sistema] Error al leer mensaje privado.");
                 break;
             }
             mensaje[strcspn(mensaje, "\n")] = 0;
             send_json_message_private(global_wsi, g_username, target, mensaje);
             break;
         }
         case 3:
             add_chat_line("[Sistema] Saliendo...");
             print_interface();
             return;
         default:
             add_chat_line("[Sistema] Opción inválida.");
             break;
         }
     }
 }
 
 //-----------------------------------------------------------------------------
 // main
 //-----------------------------------------------------------------------------
 static void* service_loop(void* arg);
 
 int main() {
     lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, NULL);
 
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = CONTEXT_PORT_NO_LISTEN;
 
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
 
     // Crear contexto
     struct lws_context *context = lws_create_context(&info);
     if (!context) {
         fprintf(stderr, "[main] Error al crear el contexto\n");
         return -1;
     }
     global_context = context;
 
     // Crear hilo para lws_service
     pthread_create(&service_thread_id, NULL, service_loop, (void*)context);
 
     // Conectarse al servidor
     struct lws_client_connect_info ccinfo = {
         .context = context,
         .address = "localhost",
         .port = 8080,
         .path = "/chat",
         .host = "localhost",
         .origin = "localhost",
         .protocol = "chat_protocol",
         .ssl_connection = 0
     };
 
     struct lws *wsi = lws_client_connect_via_info(&ccinfo);
     if (!wsi) {
         fprintf(stderr, "[main] Fallo al conectar\n");
         stop_service = 1;
         pthread_join(service_thread_id, NULL);
         lws_context_destroy(context);
         return -1;
     }
 
     // Esperar a que se establezca la conexión
     while (!g_connection_established) {
         usleep(50000);
     }
 
     // Registrar usuario
     printf("Ingresa tu nombre de usuario para registrarte: ");
     scanf("%s", g_username);
     send_json_message(wsi, "register", g_username, "Registro automático al iniciar");
 
     // Menú interactivo
     menu_interactivo();
 
     // Salimos del menú => detener el hilo de servicio
     stop_service = 1;
     pthread_join(service_thread_id, NULL);
 
     // Cerrar contexto
     lws_context_destroy(context);
     return 0;
 }
 
