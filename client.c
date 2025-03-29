/******************************************************************************
 * client.c
 * Cliente WebSocket en C con hilo aparte para lws_service,
 * incluyendo opciones extra:
 *  - change_status (ACTIVO/OCUPADO/INACTIVO)
 *  - list_users (lista de usuarios y estados)
 *  - user_info (IP y estado de un usuario)
 *  - disconnect (cierra sesión)
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
 // Estructura por sesión (aunque aquí la usemos mínimo)
 struct per_session_data__chat {
     char *username;
 };
 
 //-----------------------------------------------------------------------------
 // Variables globales
 //-----------------------------------------------------------------------------
 static struct lws *global_wsi = NULL;
 static int g_connection_established = 0;
 static char g_username[100] = "invitado";
 
 // Almacenar las últimas líneas del “chat log” (o mensajes en general)
 static char chat_log[MAX_CHAT_LINES][256];
 static int  chat_count = 0;
 
 // Hilo de servicio
 static volatile int stop_service = 0;
 static pthread_t service_thread_id;
 static struct lws_context *global_context = NULL;
 
 //-----------------------------------------------------------------------------
 // add_chat_line: agrega una línea al array chat_log, desplazando si está lleno
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
 
 //-----------------------------------------------------------------------------
 // print_interface: limpia pantalla y muestra el chat + menú
 //-----------------------------------------------------------------------------
 static void print_interface() {
     system("clear");
     printf("========== CHAT LOG (últimas %d líneas) ==========\n", MAX_CHAT_LINES);
     for (int i = 0; i < chat_count; i++) {
         printf("%s\n", chat_log[i]);
     }
     printf("==================================================\n\n");
 
     printf("=== MENÚ ===\n");
     printf("1) Enviar mensaje (broadcast)\n");
     printf("2) Enviar mensaje privado\n");
     printf("3) Cambiar estado (ACTIVO/OCUPADO/INACTIVO)\n");
     printf("4) Listar usuarios\n");
     printf("5) Info de usuario\n");
     printf("6) Desconectar (cerrar sesión)\n");
     printf("7) Salir del programa\n");
     printf("Selecciona una opción: ");
     fflush(stdout);
 }
 
 //-----------------------------------------------------------------------------
 // service_loop: hilo secundario para lws_service
 //-----------------------------------------------------------------------------
 static void* service_loop(void* arg) {
     struct lws_context *ctx = (struct lws_context *)arg;
     while (!stop_service) {
         lws_service(ctx, 50);
         usleep(5000); // Evita usar 100% CPU
     }
     return NULL;
 }
 
 //-----------------------------------------------------------------------------
 // Funciones de envío de mensajes JSON
 //-----------------------------------------------------------------------------
 
 // Broadcast / Mensaje general
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
         fprintf(stderr, "[send_json_message] Error al enviar (ret=%d)\n", written);
         return -1;
     }
 
     // Para que localmente veamos la acción
     char temp[256];
     snprintf(temp, sizeof(temp), "[Tú->Broadcast] %s", content);
     add_chat_line(temp);
     print_interface();
     return 0;
 }
 
 // Mensaje privado
 static int send_json_message_private(struct lws *wsi, const char *sender,
                                      const char *target, const char *content)
 {
     if (!wsi) {
         fprintf(stderr, "[send_json_message_private] wsi NULL\n");
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
         fprintf(stderr, "[send_json_message_private] Error (ret=%d)\n", written);
         return -1;
     }
 
     char temp[256];
     snprintf(temp, sizeof(temp), "[Tú->%s] %s", target, content);
     add_chat_line(temp);
     print_interface();
     return 0;
 }
 
 // Cambiar estado
 static int send_json_change_status(struct lws *wsi, const char *sender, const char *nuevo_estado)
 {
     if (!wsi) {
         fprintf(stderr, "[send_json_change_status] wsi NULL\n");
         return -1;
     }
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     // {type:"change_status", sender:"...", content:"ACTIVO/OCUPADO/INACTIVO"}
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"change_status\",\"sender\":\"%s\",\"content\":\"%s\"}",
              sender, nuevo_estado);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_change_status] Error (ret=%d)\n", written);
         return -1;
     }
 
     char temp[256];
     snprintf(temp, sizeof(temp), "[Estado] Cambiando a %s", nuevo_estado);
     add_chat_line(temp);
     print_interface();
     return 0;
 }
 
 // Listar usuarios
 static int send_json_list_users(struct lws *wsi, const char *sender)
 {
     if (!wsi) return -1;
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     // {type:"list_users", sender:"..."}
     snprintf(json_part, MAX_PAYLOAD_SIZE,
        "{\"type\":\"list_users\",\"sender\":\"%s\",\"content\":null}", sender);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_list_users] Error ret=%d\n", written);
         return -1;
     }
     return 0;
 }
 
 // user_info (IP y estado de un usuario)
 static int send_json_user_info(struct lws *wsi, const char *sender, const char *target)
 {
     if (!wsi) return -1;
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     // {type:"user_info", sender:"...", target:"..."}
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"user_info\",\"sender\":\"%s\",\"target\":\"%s\"}",
              sender, target);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_user_info] Error ret=%d\n", written);
         return -1;
     }
     return 0;
 }
 
 // Desconectarse
 static int send_json_disconnect(struct lws *wsi, const char *sender)
 {
     if (!wsi) return -1;
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
 
     // {type:"disconnect", sender:"...", content:"Cierre de sesión"}
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"disconnect\",\"sender\":\"%s\",\"content\":\"Cierre de sesión\"}",
              sender);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[send_json_disconnect] Error ret=%d\n", written);
         return -1;
     }
     add_chat_line("[Sistema] Solicitud de desconexión enviada");
     print_interface();
     return 0;
 }
 
 //-----------------------------------------------------------------------------
 // Callback lws
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
                 // Mensaje no-JSON
                 char raw_line[256];
                 snprintf(raw_line, sizeof(raw_line),
                          "[Recibido RAW]: %.*s", (int)len, (char*)in);
                 add_chat_line(raw_line);
                 print_interface();
                 break;
             }
 
             // Campos: type, sender, content, ...
             struct json_object *jtype, *jsender, *jcontent, *jusers, *jtarget;
             json_object_object_get_ex(parsed, "type", &jtype);
             json_object_object_get_ex(parsed, "sender", &jsender);
             json_object_object_get_ex(parsed, "content", &jcontent);
             // Para list_users
             json_object_object_get_ex(parsed, "content", &jusers);
             // user_info_response
             json_object_object_get_ex(parsed, "target", &jtarget);
 
             const char *type_str    = jtype    ? json_object_get_string(jtype)    : NULL;
             const char *sender_str  = jsender  ? json_object_get_string(jsender)  : NULL;
             const char *content_str = jcontent ? json_object_get_string(jcontent) : NULL;
 
             if (type_str && strcmp(type_str, "chat") == 0) {
                 // Broadcast
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[msg público] %s: %s",
                          sender_str ? sender_str : "???",
                          content_str ? content_str : "");
                 add_chat_line(line);
             }
             else if (type_str && strcmp(type_str, "private") == 0) {
                 // Mensaje privado
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[msg privado] %s te dice: %s",
                          sender_str ? sender_str : "???",
                          content_str ? content_str : "");
                 add_chat_line(line);
             }
             else if (type_str && strcmp(type_str, "register_success") == 0) {
                 add_chat_line("[Sistema] Registro exitoso");
             }
             else if (type_str && strcmp(type_str, "status_update") == 0) {
                 // content: {"user":"...", "status":"..."}
                 struct json_object *jcobj = jcontent; 
                 if (jcobj) {
                     struct json_object *jusr, *jst;
                     json_object_object_get_ex(jcobj, "user", &jusr);
                     json_object_object_get_ex(jcobj, "status", &jst);
 
                     const char *u = jusr ? json_object_get_string(jusr):"???";
                     const char *s = jst  ? json_object_get_string(jst) :"???";
                     char line[256];
                     snprintf(line, sizeof(line),
                              "[Sistema] %s cambió su estado a %s", u, s);
                     add_chat_line(line);
                 }
             }
             else if (type_str && strcmp(type_str, "list_users_response") == 0) {
                if (jcontent && json_object_is_type(jcontent, json_type_array)) {
                    int arr_len = json_object_array_length(jcontent);
                    char line[256];
                    snprintf(line, sizeof(line), "[Usuarios] Lista (%d):", arr_len);
                    add_chat_line(line);
            
                    for (int i = 0; i < arr_len; i++) {
                        struct json_object *juser = json_object_array_get_idx(jcontent, i);
                        const char *uname = juser ? json_object_get_string(juser) : "???";
                        char user_line[256];
                        snprintf(user_line, sizeof(user_line), " - %s", uname);
                        add_chat_line(user_line);
                    }
                }
            }
             else if (type_str && strcmp(type_str, "user_info_response") == 0) {
                 // "content": {"ip":"...", "status":"..."}, "target":"usuario"
                 struct json_object *jcont;
                 json_object_object_get_ex(parsed, "content", &jcont);
                 if (jcont) {
                     struct json_object *jip, *jst;
                     json_object_object_get_ex(jcont, "ip", &jip);
                     json_object_object_get_ex(jcont, "status", &jst);
                     const char *ip_str = jip ? json_object_get_string(jip) :"???";
                     const char *st_str = jst ? json_object_get_string(jst) :"???";
 
                     const char *targ_str = jtarget ? json_object_get_string(jtarget) : "???";
                     char line[256];
                     snprintf(line, sizeof(line),
                              "[Info] %s => IP: %s, Estado: %s", targ_str, ip_str, st_str);
                     add_chat_line(line);
                 }
             }
             else if (type_str && strcmp(type_str, "user_disconnected") == 0) {
                 // content: "<nombre_usuario> ha salido"
                 // El server forzará cierre
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[Sistema] %s", content_str ? content_str : "(desconectado)");
                 add_chat_line(line);
             }
             else {
                 // Mostrar tal cual
                 char line[256];
                 snprintf(line, sizeof(line),
                          "[Recibido] %.*s", (int)len, (char *)in);
                 add_chat_line(line);
             }
 
             json_object_put(parsed);
             print_interface();
         } else {
             add_chat_line("[Sistema] Mensaje vacío o nulo");
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
 // Menú
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
         // Consumir salto
         fgetc(stdin);
 
         if (!g_connection_established || !global_wsi) {
             add_chat_line("[Sistema] Conexión no establecida aún.");
             continue;
         }
 
         switch (opcion) {
         case 1: {
             // broadcast
             char mensaje[256];
             printf("Ingresa el mensaje a enviar (broadcast): ");
             if (fgets(mensaje, sizeof(mensaje), stdin) == NULL) {
                 add_chat_line("[Sistema] Error al leer broadcast.");
                 break;
             }
             mensaje[strcspn(mensaje, "\n")] = 0;
             // Usamos 'broadcast' en lugar de 'chat' si quieres apegarte EXACTO al PDF
             send_json_message(global_wsi, "broadcast", g_username, mensaje);
             break;
         }
         case 2: {
             // privado
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
                 add_chat_line("[Sistema] Error al leer msg privado.");
                 break;
             }
             mensaje[strcspn(mensaje, "\n")] = 0;
 
             // 'private' apega 100% al PDF
             send_json_message_private(global_wsi, g_username, target, mensaje);
             break;
         }
         case 3: {
             // cambiar estado
             printf("Estado (ACTIVO/OCUPADO/INACTIVO): ");
             char nuevo_est[32];
             if (scanf("%s", nuevo_est) != 1) {
                 fseek(stdin, 0, SEEK_END);
                 add_chat_line("[Sistema] Error al leer estado.");
                 break;
             }
             fgetc(stdin);
             // mandar change_status
             // {type:"change_status",sender:"...",content:"ACTIVO/OCUPADO/INACTIVO"}
                send_json_change_status(global_wsi, g_username, nuevo_est);
                char msg[256];
                snprintf(msg, sizeof(msg), "%s ha cambiado su estado a %s", g_username, nuevo_est);
                send_json_message(global_wsi, "broadcast", g_username, msg);

             break;
         }
         case 4: {
             // list_users
             send_json_list_users(global_wsi, g_username);
             break;
         }
         case 5: {
             // user_info
             printf("Usuario a consultar: ");
             char targ[100];
             if (scanf("%s", targ) != 1) {
                 fseek(stdin, 0, SEEK_END);
                 add_chat_line("[Sistema] Error al leer usuario info.");
                 break;
             }
             fgetc(stdin);
             send_json_user_info(global_wsi, g_username, targ);
             break;
         }
         case 6: {
             // desconectar
             // {type:"disconnect", sender:"...", content:"Cierre de sesión"}
                send_json_disconnect(global_wsi, g_username);
                char msg[256];
                snprintf(msg, sizeof(msg), "%s ha cerrado sesión", g_username);
                send_json_message(global_wsi, "broadcast", g_username, msg);
             
             break;
         }
         case 7:
             // Salir del programa localmente
             add_chat_line("[Sistema] Saliendo del programa local...");
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
             "chat-protocol",
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
         .address = "localhost",   // Ajusta la IP/host de tu servidor
         .port = 8080,
         .path = "/chat",
         .host = "localhost",
         .origin = "localhost",
         .protocol = "chat-protocol",
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
 
     // {type:"register",sender:"<username>",content:null}
     unsigned char buffer[LWS_PRE + MAX_PAYLOAD_SIZE];
     memset(buffer, 0, sizeof(buffer));
     char *json_part = (char *)&buffer[LWS_PRE];
     snprintf(json_part, MAX_PAYLOAD_SIZE,
              "{\"type\":\"register\",\"sender\":\"%s\",\"content\":null}",
              g_username);
 
     size_t msg_len = strlen(json_part);
     int written = lws_write(wsi, (unsigned char *)json_part, msg_len, LWS_WRITE_TEXT);
     if (written < (int)msg_len) {
         fprintf(stderr, "[main] Error al registrar (ret=%d)\n", written);
     }
 
     // Bucle de menú
     menu_interactivo();
 
     // Salimos => detener hilo
     stop_service = 1;
     pthread_join(service_thread_id, NULL);
 
     // Destruir contexto
     lws_context_destroy(context);
     return 0;
 }
