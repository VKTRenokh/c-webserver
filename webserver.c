#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

int sockG;
int bindedG;
int listenedG;

extern char **environ;

struct KeyValue {
  char key[50];
  char value[50];
};

struct Dictionary {
  struct KeyValue entries[100];
  int size;
};

struct Dictionary add_entry(struct Dictionary *dict, const char *key,
                            const char *value) {
  if (dict->size < sizeof(dict->entries) / sizeof(dict->entries[0])) {
    strcpy(dict->entries[dict->size].key, key);
    strcpy(dict->entries[dict->size].value, value);
    dict->size++;
  }
  return *dict;
}

const char *get_value(struct Dictionary *dict, const char *key) {
  for (int i = 0; i < dict->size; i++) {
    if (strcmp(dict->entries[i].key, key) == 0) {
      return dict->entries[i].value;
    }
  }
  return NULL;
}

const char *get_extension(char f[]) {
  char *dot_pos = strrchr(f, '.');

  if (dot_pos != NULL) {
    return dot_pos + 1;
  }

  return "";
}

void send_headers(int client, const char *content_type, size_t content_length) {
  const char *headers_format = "HTTP/1.0 200 OK\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %lu\r\n"
                               "Content-Encoding: gzip\r\n"
                               "Cache-Control: max-age-31536000\r\n\r\n";

  dprintf(client, headers_format, content_type, content_length);
}

void sigint_handler(int signum) {
  printf("caught sigint");

  if (sockG == 0 || bindedG == 0 || listenedG == 0) {
    exit(signum);
    return;
  }

  close(sockG);
  close(bindedG);
  close(listenedG);

  exit(signum);
}

struct Dictionary create_content_type_dictionary() {
  struct Dictionary dict = {0};

  add_entry(&dict, "css", "text/css");
  add_entry(&dict, "html", "text/html");
  add_entry(&dict, "js", "application/javascript");
  add_entry(&dict, "json", "application/json");
  add_entry(&dict, "ico", "image/x-icon");

  return dict;
};

char *receive_data(int client_de) {
  char *buffer = (char *)malloc(256);

  if (buffer == NULL) {
    return NULL;
  }

  ssize_t bytes_received = recv(client_de, buffer, 256, 0);

  if (bytes_received == 0) {
    free(buffer);
    return NULL;
  }

  if (bytes_received < 0) {
    free(buffer);
    return NULL;
  }

  char *resized = (char *)realloc(buffer, bytes_received);

  if (resized == NULL) {
    free(buffer);
    return NULL;
  }

  buffer = resized;

  return buffer;
}

struct stat get_file_stat(char *file_path) {
  struct stat st;

  int s = stat(file_path, &st);

  if (s == -1) {
    perror("cannot read file stat");
  }

  return st;
}

void handle_client(int client, struct Dictionary content_types) {
  char *buffer = receive_data(client);

  char *file_path = strchr(buffer, '/') + 1;

  *strchr(file_path, ' ') = 0;

  int file = open(file_path, O_RDONLY);

  struct stat st = get_file_stat(file_path);

  const char *content_type =
      get_value(&content_types, get_extension(file_path));

  const char *accept_encoding = strstr(buffer, "Accept-Encoding: ");

  if (accept_encoding != NULL && strstr(accept_encoding, "gzip") != NULL) {
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
      perror("pipe");
      close(client);
      close(file);
    }
  }

  send_headers(client, content_type, st.st_size);

  sendfile(client, file, 0, st.st_size);

  close(file);
}

void start_server(int sock) {
  struct Dictionary content_types = create_content_type_dictionary();

  while (1) {
    int client = accept(sock, 0, 0);

    handle_client(client, content_types);

    close(client);
  }
}

int create_sock() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1) {
    perror("cannot create sock");
    close(sock);
    return -1;
  }

  const struct sockaddr_in addr = {
      AF_INET,
      htons(8080),
      0,
  };

  int binded = bind(sock, &addr, sizeof(addr));

  if (binded == -1) {
    perror("cannot bind");
    close(sock);
    close(binded);
    return -1;
  }

  int listened = listen(sock, 10);

  if (listened == -1) {
    close(sock);
    close(binded);
    close(listened);
    return -1;
  }

  sockG = sock;
  bindedG = binded;
  listenedG = listened;

  return sock;
}

int main(int argc, char *argv[]) {
  int sock = create_sock();
  start_server(sock);

  signal(SIGINT, sigint_handler);

  return 1;
}
