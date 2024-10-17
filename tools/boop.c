#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define MAX_PAYLOAD_SIZE 4096
#define CHUNK_SIZE 32
#define PORT 8080
#define ACCEPTED_EXTENSIONS ".cia,.tik,.cetk,.3dsx"

void handle_sigint(int sig) {
	printf("\nTerminating server...\n");
	exit(0);  // Exit the program cleanly
}

// Function to detect host IP automatically
void detect_host_ip(char *host_ip, size_t size) {
	struct ifaddrs *ifaddr, *ifa;
	int family;
	if (getifaddrs(&ifaddr) == -1) {
		perror("Failed to detect host IP");
		exit(EXIT_FAILURE);
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;
		if (family == AF_INET) {  // IPv4 address
			getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host_ip, size, NULL, 0, NI_NUMERICHOST);
			break;
		}
	}

	freeifaddrs(ifaddr);
}

// Function to start a local HTTP server using Python
void *start_http_server(void *directory) {
	char command[BUFFER_SIZE];
	snprintf(command, sizeof(command), "python3 -m http.server %d --directory %s", PORT, (char *)directory);
	system(command);  // Execute Python HTTP server in the specified directory
	return NULL;
}

// Function to check if a file has an accepted extension
int has_accepted_extension(const char *filename) {
	const char *extensions[] = { ".cia", ".tik", ".cetk", ".3dsx" };
	size_t num_ext			 = sizeof(extensions) / sizeof(extensions[0]);

	for (size_t i = 0; i < num_ext; ++i) {
		if (strstr(filename, extensions[i]) != NULL) {
			return 1;
		}
	}
	return 0;
}

// Function to prepare the payload (list of URLs) based on file or directory
// Modify the file URL generation to use relative paths properly
// Modify the file URL generation to use relative paths properly
void prepare_file_list_payload(const char *target_path, char *host_ip, char *file_list_payload, size_t max_payload_size) {
	struct stat path_stat;
	stat(target_path, &path_stat);
	file_list_payload[0] = '\0';  // Initialize the payload

	if (S_ISREG(path_stat.st_mode)) {
		// If it's a file
		if (has_accepted_extension(target_path)) {
			// Extract just the filename
			const char *filename = strrchr(target_path, '/');
			filename			 = (filename) ? filename + 1 : target_path;

			snprintf(file_list_payload, max_payload_size, "http://%s:%d/%s\n", host_ip, PORT, filename);
		} else {
			fprintf(stderr, "Unsupported file extension: %s\n", target_path);
			exit(EXIT_FAILURE);
		}
	} else if (S_ISDIR(path_stat.st_mode)) {
		// If it's a directory
		DIR *dir = opendir(target_path);
		if (!dir) {
			perror("Failed to open directory");
			exit(EXIT_FAILURE);
		}

		struct dirent *entry;
		while ((entry = readdir(dir)) != NULL) {
			if (has_accepted_extension(entry->d_name)) {
				strncat(file_list_payload, "http://", max_payload_size - strlen(file_list_payload) - 1);
				strncat(file_list_payload, host_ip, max_payload_size - strlen(file_list_payload) - 1);
				snprintf(file_list_payload + strlen(file_list_payload), max_payload_size - strlen(file_list_payload), ":%d/%s\n", PORT,
						 entry->d_name);
			}
		}
		closedir(dir);
	}
}

// Function to send file URLs to the 3DS
void send_to_3ds(const char *target_ip, const char *file_list_payload) {
	int sock;
	struct sockaddr_in server_addr;

	// Create socket
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port   = htons(5000);  // Target port on the 3DS
	inet_pton(AF_INET, target_ip, &server_addr.sin_addr);

	// Connect to the 3DS
	if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("Connection to 3DS failed");
		close(sock);
		exit(EXIT_FAILURE);
	}

	// Send the length of the payload
	uint32_t payload_length = htonl(strlen(file_list_payload));
	if (send(sock, &payload_length, sizeof(payload_length), 0) < 0) {
		perror("Failed to send payload length");
		close(sock);
		exit(EXIT_FAILURE);
	}

	// Send the payload
	size_t bytes_sent	= 0;
	size_t payload_size = strlen(file_list_payload);
	while (bytes_sent < payload_size) {
		ssize_t sent = send(sock, file_list_payload + bytes_sent, CHUNK_SIZE, 0);
		if (sent < 0) {
			perror("Failed to send payload data");
			close(sock);
			exit(EXIT_FAILURE);
		}
		bytes_sent += sent;
	}

	// Wait for acknowledgment from the 3DS
	char buffer[1];
	if (recv(sock, buffer, 1, 0) < 0) {
		perror("Failed to receive acknowledgment");
		close(sock);
		exit(EXIT_FAILURE);
	}

	// Close the connection
	close(sock);
}

int main(int argc, char *argv[]) {
	char target_ip[INET_ADDRSTRLEN];
	char target_path[BUFFER_SIZE];
	char host_ip[INET_ADDRSTRLEN];
	char file_list_payload[MAX_PAYLOAD_SIZE] = { 0 };

	signal(SIGINT, handle_sigint);

	// Argument parsing
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <target ip> <file / directory> [host ip]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Target 3DS IP
	strncpy(target_ip, argv[1], sizeof(target_ip) - 1);
	target_ip[sizeof(target_ip) - 1] = '\0';

	// Target path (file or directory)
	strncpy(target_path, argv[2], sizeof(target_path) - 1);
	target_path[sizeof(target_path) - 1] = '\0';

	// Host IP (can be provided or detected automatically)
	if (argc == 4) {
		strncpy(host_ip, argv[3], sizeof(host_ip) - 1);
		host_ip[sizeof(host_ip) - 1] = '\0';
	} else {
		detect_host_ip(host_ip, sizeof(host_ip));
	}

	// Prepare the file list payload
	prepare_file_list_payload(target_path, host_ip, file_list_payload, sizeof(file_list_payload));

	// Start the HTTP server
	pthread_t server_thread;
	pthread_create(&server_thread, NULL, start_http_server, target_path);

	// Allow time for the server to start
	sleep(1);

	// Send the file URLs to the 3DS
	send_to_3ds(target_ip, file_list_payload);

	// Wait for the server thread to finish
	pthread_join(server_thread, NULL);

	return 0;
}
