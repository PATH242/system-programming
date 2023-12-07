#include "chat.h"
#include "chat_server.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#define _GNU_SOURCE
#define BUF_SIZE 1024

struct chat_peer {
	/** Client's socket. To read/write messages. */
	int socket;
	char* name;
	/** Output buffer. */
	char* output_buf;
	int output_buf_size;
	int output_buf_capacity;
};

struct chat_server {
	/** Listening socket. To accept new clients. */
	int socket;
	/** Array of peers. */
	struct chat_peer** peers;
	int n_peers;
	/** Array of received messages. */
	int n_received_messages;
	struct chat_message** received_messages;
	/** Input buffer. */
	char* input_buf;
	int input_buf_complete_messages_n;
	int input_buf_size;
	int input_buf_capacity;

	struct epoll_event epoll_trigger;
	int epoll_fd;
};

struct chat_server *
chat_server_new(void)
{
	struct chat_server *server = malloc(sizeof(*server));
	if(server == NULL)
	{
		abort();
	}
	server->socket = -1;
	server->n_peers = 0;
	server->peers = NULL;
	server->input_buf = NULL;
	server->input_buf_size = 0;
	server->input_buf_capacity = 0;
	server->n_received_messages = 0;
	server->received_messages = NULL;
	server->input_buf_complete_messages_n = 0;
	return server;
}

void
chat_server_delete(struct chat_server *server)
{
	if (server->socket >= 0)
	{
		close(server->socket);
	}
	for(int i = 0; i < server->n_peers; i++)
	{
		if(server->peers[i]->name != NULL)
		{
			free(server->peers[i]->name);
		}
		if(server->peers[i]->output_buf != NULL)
		{
			free(server->peers[i]->output_buf);
		}
		free(server->peers[i]);
	}
	if(server->n_peers)
	{
		free(server->peers);
	}
	if(server->input_buf_capacity)
	{
		free(server->input_buf);
	}
	for(int i = 0; i < server->n_received_messages; i++)
	{
		chat_message_delete(server->received_messages[i]);
	}
	if(server->received_messages != NULL)
	{
		free(server->received_messages);
	}
	if(server->epoll_fd >= 0)
	{
		close(server->epoll_fd);
	}
	free(server);
}

void delete_peer(struct chat_server* server, int fd)
{
	short int shift = 0;
	for(int i = 0; i < server->n_peers; i++)
	{
		if(server->peers[i]->socket == fd)
		{
			if(server->peers[i]->output_buf_capacity)
			{
				free(server->peers[i]->output_buf);
			}
			if(server->peers[i]->name != NULL)
			{
				free(server->peers[i]->name);
			}
			free(server->peers[i]);
			shift = 1;
		}
		if(shift && i + 1 < server->n_peers)
		{
			server->peers[i] = server->peers[i+1];
		}
	}
	if(shift)
	{
		server->n_peers --;
		server->peers = realloc(server->peers, server->n_peers * sizeof(struct chat_peer*));
	}
	return;
}

int accept_new_peer(struct chat_server* server)
{
	while(1)
	{
		int new_client_fd = accept(server->socket, NULL, NULL);
		if (new_client_fd == -1) {
			/* We have processed all incoming connections. */
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				break;
			}
			else {
				perror ("Error accepting client at server");
				break;
			}
		}
		if(new_client_fd < 0)
		{
			return CHAT_ERR_SYS;
		}
		struct chat_peer* new_peer = malloc(sizeof(struct chat_peer));
		new_peer->name = NULL;
		new_peer->socket = new_client_fd;
		new_peer->output_buf_size = 0;
		new_peer->output_buf_capacity = 0;
		new_peer->output_buf = NULL;

		server->peers = realloc(server->peers, (server->n_peers + 1) * sizeof(struct chat_peer*));
		server->peers[server->n_peers] = new_peer;
		server->n_peers += 1;

		struct epoll_event event;
		event.events = (EPOLLIN | EPOLLET);
		event.data.fd = new_client_fd;
		// Make the new connection non blocking
		int old_flags = fcntl(new_client_fd, F_GETFL);
		fcntl(new_client_fd, F_SETFL, old_flags | O_NONBLOCK);
		if(epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, new_client_fd, &event) < 0)
		{
			epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, new_client_fd, NULL);
			close(new_client_fd);
			free(new_peer);
			return CHAT_ERR_SYS;
		}
	}
	return 0;
}

struct chat_peer* find_peer(struct chat_server* server, int client_fd)
{
	for(int i = 0; i < server->n_peers; i++)
	{
		if(server->peers[i]->socket == client_fd)
		{
			return server->peers[i];
		}
	}
	return NULL;
}

/*
 * 1) Create a server socket (function socket()).
 * 2) Bind the server socket to addr (function bind()).
 * 3) Listen the server socket (function listen()).
 * 4) Create epoll
 */
int
chat_server_listen(struct chat_server *server, uint16_t port)
{
	if (server->socket != -1)
	{
        return CHAT_ERR_ALREADY_STARTED;
    }
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(port);
	/* Listen on all IPs of this machine. */
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server->socket  = socket(AF_INET, SOCK_STREAM, 0);

	if (server->socket < 0) {
        perror("Error creating server socket");
        return CHAT_ERR_SYS;
    }
    int socket_options = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &socket_options, sizeof(int)) < 0)
    {
        perror("Error setting server socket options");
        close(server->socket);
        return CHAT_ERR_SYS;
    }
	int old_flags = fcntl(server->socket, F_GETFL);
	fcntl(server->socket, F_SETFL, old_flags | O_NONBLOCK);
    if (bind(server->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Error binding server socket");
		if (errno == EADDRINUSE) {
            return CHAT_ERR_PORT_BUSY;
        }
        return CHAT_ERR_SYS;
    }
	if(listen(server->socket, SOMAXCONN) < 0)
	{
		perror("Error listening on server");
		close(server->socket);
		return CHAT_ERR_SYS;
	}
	
	int epoll_fd = epoll_create(1);
	server->epoll_fd = epoll_fd;
	if (epoll_fd < 0)
	{
		perror("Error creating epoll\n");
		close(server->socket);
		return CHAT_ERR_SYS;
	}
	server->epoll_trigger.events = (EPOLLIN | EPOLLET);
	server->epoll_trigger.data.fd = server->socket;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->socket, &server->epoll_trigger) < 0)
    {
		perror("Error adding server socket to epoll");
        close(server->socket);
		close(epoll_fd);
        return CHAT_ERR_SYS;
    }
    return 0;
}

struct chat_message *
chat_server_pop_next(struct chat_server *server)
{
	if(server->n_received_messages)
	{
		struct chat_message* ret = server->received_messages[0];
		for(int i = 1; i < server->n_received_messages; i++)
		{
			server->received_messages[i-1]
				= server->received_messages[i];
		}
		server->n_received_messages--;
		server->received_messages = 
			realloc(server->received_messages, (server->n_received_messages) * sizeof(struct chat_message*));
		return ret;
	}
	return NULL;
}

int send_message_to_client(struct chat_server* server, int client_socket)
{
	struct chat_peer* client = find_peer(server, client_socket);
	if(client == NULL)
	{
		return -1;
	}
	if(client->output_buf_size == 0)
	{
		return 0;
	}
	int pointer = 0;
	int rc = 0;
	while(pointer < client->output_buf_size)
	{
		rc = 
			send(client_socket, client->output_buf + pointer,
					client->output_buf_size - pointer, 0);
		if(rc < 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				// Calculate the number of characters to keep
        		size_t remaining = client->output_buf_size - pointer + 1; // +1 to include the null terminator
		        memmove(client->output_buf, client->output_buf + pointer, remaining);
				client->output_buf[client->output_buf_size - pointer] = '\0';
				return pointer;
			}
		}
		else 
		if(rc == 0)
		{
			break;
		}
		else
		{
			pointer += rc;
		}
	}
	server->epoll_trigger.events = (EPOLLET | EPOLLIN);
	server->epoll_trigger.data.fd = client_socket;
	rc = epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client_socket, &server->epoll_trigger);
	client->output_buf_size = 0;
	client->output_buf_capacity = 0;
	free(client->output_buf);
	client->output_buf = NULL;
	if(rc < 0)
	{
		return CHAT_ERR_SYS;
	}
	return 0;
}

void concatenate_authors_and_messages(struct chat_server* server)
{
	if (!server->input_buf_complete_messages_n)
	{
		return;
	}
	/*Not the most effecient way but I wanted to keep the
	bonus functionality somewhat separate from main functioniality.*/
	char* result_buf = calloc(BUF_SIZE, sizeof(char));
	int result_buf_size = 0;
	int result_capacity = BUF_SIZE;
	int offset = server->n_received_messages
			- server->input_buf_complete_messages_n;
	for (int i = 0; i < server->input_buf_complete_messages_n; i++)
	{
		// Concatenate author and message
		int author_len = strlen(server->received_messages[i + offset]->author);
		int data_len = strlen(server->received_messages[i + offset]->data);
		if (result_buf_size + author_len + data_len + 2 > result_capacity)
		{
			result_capacity = result_buf_size + author_len + data_len + 2;
			char* new_buf = calloc(result_capacity, sizeof(char));
			memcpy(new_buf, result_buf, result_buf_size);
			free(result_buf);
			result_buf = new_buf;
		}

		memcpy(result_buf + result_buf_size, server->received_messages[i + offset]->author, author_len);
		result_buf_size += author_len;
		result_buf[result_buf_size++] = '\n';

		memcpy(result_buf + result_buf_size, server->received_messages[i + offset]->data, data_len);
		result_buf_size += data_len;
		result_buf[result_buf_size++] = '\n';
	}

	free(server->input_buf);
	server->input_buf = result_buf;
	server->input_buf_size = result_buf_size;
	server->input_buf_capacity = result_capacity;
	server->input_buf_complete_messages_n = 0; 
}

int send_message_to_clients(struct chat_server* server, struct epoll_event* event)
{
	// printf("tryna send messages to all clients and all for this client: %d\n", event->data.fd);
	concatenate_authors_and_messages(server);
	for(int i = 0; i < server->n_peers; i++)
	{
		int client_socket = server->peers[i]->socket;
		if(client_socket == event->data.fd)
		{
			continue;
		}
		struct chat_peer* client = find_peer(server, client_socket);
		int target_size = client->output_buf_size + server->input_buf_size;
		char* new_buf = calloc(target_size, sizeof(char));
		memcpy(new_buf, client->output_buf, client->output_buf_size);
		memcpy(new_buf + client->output_buf_size, server->input_buf, server->input_buf_size);
		free(client->output_buf);
		client->output_buf = new_buf;
		client->output_buf_size = target_size;
		client->output_buf_capacity = target_size;
		// signal to all sockets that they should write
		server->epoll_trigger.events = (EPOLLOUT | EPOLLIN | EPOLLET);
		server->epoll_trigger.data.fd = client_socket;
		int rc = epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client_socket, &server->epoll_trigger);
		if(rc < 0)
		{
			return CHAT_ERR_SYS;
		}
	}
	free(server->input_buf);
	server->input_buf_capacity = 0;
	server->input_buf_size = 0;
	return 0;
}

/*
* 1) Read message from client with as many retries as it takes
* 2) store message somewhere for pop_message to access it
*/
int read_message_from_client(struct chat_server* server, struct epoll_event* event)
{
	int client_fd = event->data.fd;
	struct chat_peer* client = find_peer(server, client_fd);
	int rc = 0;
	int total_received = 0;
	server->input_buf = calloc(BUF_SIZE, sizeof(char));
	server->input_buf_capacity = BUF_SIZE;
	do{
		server->input_buf_size += rc;
		total_received += rc;
		if(server->input_buf_size == server->input_buf_capacity)
		{
			server->input_buf_capacity *= 2;
			char* new_buf = calloc(server->input_buf_capacity, sizeof(char));
			memcpy(new_buf, server->input_buf, server->input_buf_size);
			free(server->input_buf);
			server->input_buf = new_buf;
		}
		rc = recv(client_fd, server->input_buf + server->input_buf_size,
						server->input_buf_capacity - server->input_buf_size, MSG_DONTWAIT);
	}while(rc > 0);

	if(total_received)
	{
		int start = 0;
		for(int i = 0; i < server->input_buf_size; i++)
		{
			if(server->input_buf[i] == '\n')
			{
				// Only first time client sends a message, get client name.
				if(client->name == NULL)
				{
					client->name = calloc(i - start + 1, sizeof(char));
					client->name = memcpy(client->name, server->input_buf, i + 1);
					// add terminating character instead of '\n'
					client->name[i] = '\0';
					// remove name from buf to not be added to other clients.
					char* new_buf = calloc(server->input_buf_capacity - i, sizeof(char));
					memcpy(new_buf, server->input_buf+i+1, server->input_buf_size -i-1);
					server->input_buf_capacity -= i;
					server->input_buf_size -= (i+1);
					start = 0;
					i = -1;
					free(server->input_buf);
					server->input_buf = new_buf;
					continue;
				}
				struct chat_message* new_message = malloc(sizeof(struct chat_message));
				char* message_content = calloc(i - start + 1, sizeof(char));
				message_content = memcpy(message_content, server->input_buf+start, (i - start +1));
				// add terminating character instead of '\n'
				message_content[i-start] = '\0';
				server->input_buf_complete_messages_n ++;
				new_message->data = message_content;
				new_message->author = strdup(client->name);
				server->received_messages = realloc(
								server->received_messages, (server->n_received_messages + 1) * sizeof(struct chat_message*));
				server->received_messages[server->n_received_messages] = new_message;
				server->n_received_messages += 1;
				start = i + 1;
			}
		}
	}
	return total_received;
}

/*
 * 1) Wait on epoll/kqueue/poll for update on any socket.
 * 2) Handle the update.
 */
int
chat_server_update(struct chat_server *server, double timeout)
{
	if(server->socket == -1)
	{
		return CHAT_ERR_NOT_STARTED;
	}
	if(timeout < 0)
	{
		timeout = 0;
	}
	int max_events = server->n_peers + 1;
	struct epoll_event *events;
	events = calloc(max_events, sizeof(struct epoll_event));
	int rc = epoll_wait(server->epoll_fd, events, max_events, timeout * 1000);
	if(rc == 0)
	{
		free(events);
		return CHAT_ERR_TIMEOUT;
	}
	else if(rc < 0)
	{
		perror("Error getting events at server update");
		free(events);
		return CHAT_ERR_SYS;
	}
	// printf("I allegedly received %d events\n", rc);
	for(int i = 0; i < max_events; i++)
	{
		if(events[i].data.fd == server->socket)
		{
			accept_new_peer(server);
			continue;
		}
		if((events[i].events & EPOLLIN))
		{
			if(read_message_from_client(server, &events[i]) == 0)
			{
				rc = epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
				if(rc < 0)
				{
					return CHAT_ERR_SYS;
				}
				close(events[i].data.fd);
				delete_peer(server, events->data.fd);
			}
			send_message_to_clients(server, &events[i]);
		}
		if((events[i].events & EPOLLOUT))
		{
			send_message_to_client(server, events[i].data.fd);
		}
	}
	free(events);
	return 0;
}

int
chat_server_get_descriptor(const struct chat_server *server)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */

	/*
	 * Server has multiple sockets - own and from connected clients. Hence
	 * you can't return a socket here. But if you are using epoll/kqueue,
	 * then you can return their descriptor. These descriptors can be polled
	 * just like sockets and will return an event when any of their owned
	 * descriptors has any events.
	 *
	 * For example, assume you created an epoll descriptor and added to
	 * there a listen-socket and a few client-sockets. Now if you will call
	 * poll() on the epoll's descriptor, then on return from poll() you can
	 * be sure epoll_wait() can return something useful for some of those
	 * sockets.
	 */
#endif
	(void)server;
	return -1;
}

int
chat_server_get_socket(const struct chat_server *server)
{
	return server->socket;
}

int
chat_server_get_events(const struct chat_server *server)
{
	(void)server;
	if(server->socket == -1)
	{
		return 0;
	}
	for(int i = 0; i < server->n_peers; i++)
	{
		if(server->peers[i]->output_buf_size)
		{
			return (CHAT_EVENT_INPUT | CHAT_EVENT_OUTPUT);
		}
	}
	return CHAT_EVENT_INPUT;
}

int
chat_server_feed(struct chat_server *server, const char *msg, uint32_t msg_size)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */
#endif
	(void)server;
	(void)msg;
	(void)msg_size;
	return CHAT_ERR_NOT_IMPLEMENTED;
}
