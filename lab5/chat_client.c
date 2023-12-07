#include "chat.h"
#include "chat_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netdb.h>
#include <poll.h>
#define _GNU_SOURCE
#define BUF_SIZE 1024
struct chat_client {
	/** Socket connected to the server. */
	int socket;
	/** Array of received messages. */
	int n_received_messages;
	struct chat_message** received_messages;
	/** Array of to be sent messages. */
	int n_messages_to_be_sent;
	struct chat_message** messages_to_be_sent;
	/** Output buffer. */
	char* output_buf;
	int output_buf_size;
	int output_buf_capacity;
	/* PUT HERE OTHER MEMBERS */
	char* name;
	struct pollfd* poll_trigger;
	int is_name_sent;
	int some_name_sent;
};

struct chat_client *
chat_client_new(const char *name)
{

	struct chat_client* client = malloc(sizeof(struct chat_client));
	if (client == NULL)
	{
        perror("Error allocating memory for chat client\n");
        abort();
    }
	client->socket = -1;
	client->n_received_messages = 0;
	client->received_messages = NULL;
	client->n_messages_to_be_sent = 0;
	client->messages_to_be_sent = NULL;
	client->output_buf = calloc(2, sizeof(char));
	client->output_buf_size = 0;
	client->output_buf_capacity = 2;
	client->name = strdup(name);
	client->poll_trigger = calloc(1, sizeof(struct pollfd));
	client->is_name_sent = 0;
	client->some_name_sent = 0;
	return client;
}

void
chat_client_delete(struct chat_client *client)
{
	if (client->socket >= 0)
		close(client->socket);
	for(int i = 0; i < client->n_received_messages; i++)
	{
		chat_message_delete(client->received_messages[i]);
	}
	if(client->received_messages != NULL)
	{
		free(client->received_messages);
	}
	for(int i = 0; i < client->n_messages_to_be_sent; i++)
	{
		chat_message_delete(client->messages_to_be_sent[i]);
	}
	if(client->messages_to_be_sent != NULL)
	{
		free(client->messages_to_be_sent);
	}
	if(client->output_buf != NULL)
	{
		free(client->output_buf);
	}
	free(client->poll_trigger);
	free(client->name);
	free(client);
}

int
chat_client_connect(struct chat_client *client, const char *addr)
{

	if (client->socket != -1)
	{
        return CHAT_ERR_ALREADY_STARTED;
    }
	struct addrinfo *addr_info;
	struct addrinfo filter;
    memset(&filter, 0, sizeof(filter));
	filter.ai_family = AF_INET;
	filter.ai_socktype = SOCK_STREAM;
	char* addr_copy = strdup(addr);
	char *host = strtok(addr_copy, ":");
    char *port = strtok(NULL, ":");
	int rc = getaddrinfo(host, port, &filter, &addr_info);
	free(host);
	// free(port);
    if (rc != 0) {
        perror("Error resolving address");
		close(client->socket);
		freeaddrinfo(addr_info);
        return CHAT_ERR_NO_ADDR;
    }
	if(addr_info == NULL)
	{
		perror("Error finding address\n");
		close(client->socket);
		freeaddrinfo(addr_info);
        return CHAT_ERR_NO_ADDR;
	}
	for(struct addrinfo* i = addr_info; i != NULL; i = i->ai_next)
	{
		client->socket = socket(i->ai_family, i->ai_socktype, i->ai_protocol);
		rc = connect(client->socket, i->ai_addr, i->ai_addrlen);
		if(rc < 0)
		{
			close(client->socket);
			client->socket = -1;
			continue;
		}
		break;
	}
	if(client->socket == -1)
	{
		perror("Error creating socket");
        return CHAT_ERR_SYS;
	}
	if(rc != 0)
	{
		perror("port busy? or like connection failed\n");
		return CHAT_ERR_PORT_BUSY;
	}
	// Make the new connection non blocking
	int old_flags = fcntl(client->socket, F_GETFL);
	fcntl(client->socket, F_SETFL, old_flags | O_NONBLOCK);
	client->poll_trigger->fd = client->socket;
	client->poll_trigger->events = POLLIN;
	freeaddrinfo(addr_info);
	return 0;
}

struct chat_message *
chat_client_pop_next(struct chat_client *client)
{
	if(client->n_received_messages)
	{
		struct chat_message* ret = client->received_messages[0];
		for(int i = 1; i < client->n_received_messages; i++)
		{
			client->received_messages[i-1]
				= client->received_messages[i];
		}
		client->n_received_messages -= 1;
		client->received_messages = 
				realloc(client->received_messages, client->n_received_messages * sizeof(struct chat_message*));
		return ret;
	}
	return NULL;
}

int send_client_name(struct chat_client* client)
{
	// Send name to server.
	int sz = strlen(client->name);
	int rc = 0, n_sent = client->some_name_sent;
	char* msg = calloc(sz + 1, sizeof(char));
	memcpy(msg, client->name, sz);
	msg[sz] = '\n';
	sz ++;
	while(1)
	{
		rc = send(client->socket, msg+ n_sent, sz - n_sent, 0);
		if(rc < 0)
		{
			break;
		}
		n_sent += rc;
		if(n_sent == sz)
		{
			break;
		}
	}
	free(msg);
	if(errno == EAGAIN || errno == EWOULDBLOCK)
	{
		client->some_name_sent = n_sent;
		return 0;
	}
	client->is_name_sent = 1;
	return 0;
}

int chat_client_send_buf(struct chat_client* client)
{
	int rc = 0;
	int total_sent = 0;
	if(client->n_messages_to_be_sent)
	{
		int msg_sent = 0;
		char* msg = client->messages_to_be_sent[0]->data;
		int sz = strlen(msg) + 1;
		msg[sz-1] = '\n';
		while(sz)
		{
			rc = send(client->socket, msg+ msg_sent, sz, 0);
			if(rc < 0)
			{
				if(errno == EWOULDBLOCK || errno == EAGAIN)
				{
					// Calculate the number of characters to keep
					size_t remaining = sz - msg_sent + 1; // +1 to include the null terminator
					memmove(client->messages_to_be_sent[0]->data,
							client->messages_to_be_sent[0]->data + msg_sent, remaining);
					client->messages_to_be_sent[0]->data
							[client->output_buf_size - msg_sent] = '\0';
				}
				return 0;
			}
			else
			if(rc == 0)
			{
				close(client->socket);
				client->socket = -1;
				return 0;
			}
			else
			{
				sz -= rc;
				total_sent += rc;
				msg_sent += rc;
			}
		}
		chat_message_delete(client->messages_to_be_sent[0]);
		for(int i = 1; i < client->n_messages_to_be_sent; i++)
		{
			client->messages_to_be_sent[i-1] = client->messages_to_be_sent[i];
		}
		client->n_messages_to_be_sent -= 1;
		client->messages_to_be_sent = realloc(client->messages_to_be_sent,
							client->n_messages_to_be_sent * sizeof(struct chat_message*));
	}

	if(!client->n_messages_to_be_sent)
	{
		client->poll_trigger->events = POLLIN;
	}
	return total_sent;
}

int chat_client_receive_buf(struct chat_client* client)
{
	int rc = 0, n_received = 0;
	char* input_buf = calloc(BUF_SIZE, sizeof(char));
	int input_buf_capacity = BUF_SIZE;
	do{
		n_received += rc;
		if(n_received == input_buf_capacity)
		{
			input_buf_capacity *= 2;
			char* new_buf = calloc(input_buf_capacity, sizeof(char));
			memcpy(new_buf, input_buf, n_received);
			free(input_buf);
			input_buf = new_buf;
		}
		rc = recv(client->socket, input_buf + n_received, input_buf_capacity - n_received, MSG_DONTWAIT);
	}while(rc > 0);
	if(n_received == 0)
	{
		free(input_buf);
		return 0;
	}
	else
	{
		int start = 0;
		int is_first = 1;
		for(int i = 0; i < n_received; i++)
		{
			// first time it's author, second it's message.
			if(input_buf[i] == '\n')
			{
				struct chat_message* new_message;
				if(is_first)
				{
					new_message = malloc(sizeof(struct chat_message));
					new_message->data = NULL;
					client->received_messages = realloc(client->received_messages, (client->n_received_messages + 1) * sizeof(struct chat_message*));
					client->received_messages[client->n_received_messages] = new_message;
					client->n_received_messages += 1;
				}
				else
				{
					new_message = client->received_messages[client->n_received_messages-1];
				}
				char* message_content = calloc(i - start + 1, sizeof(char));
				message_content = memcpy(message_content, input_buf+start, (i - start +1));
				// add terminating character instead of '\n'
				message_content[i-start] = '\0';
				if(is_first)
				{
					new_message->author = message_content;
					is_first = 0;
				}
				else
				{
					new_message->data = message_content;
					is_first = 1;
				}
				start = i + 1;
			}
		}
	}
	free(input_buf);
	return n_received;
}

int
chat_client_update(struct chat_client *client, double timeout)
{
	if(client->socket == -1)
	{
		return CHAT_ERR_NOT_STARTED;
	}
	if(timeout < 0)
	{
		timeout = 0;
	}

	int rc = poll(client->poll_trigger, 1, (int) timeout * 1000);
	if(rc == 0)
	{
		return CHAT_ERR_TIMEOUT;
	}
	else 
	if(rc < 0)
	{
		perror("Error getting events at client update");
		return CHAT_ERR_SYS;
	}
	if(client->poll_trigger->revents & POLLOUT)
	{
		// printf("got to send buf\n");
		if(!client->is_name_sent)
		{
			send_client_name(client);
		}
		chat_client_send_buf(client);
	}
	if(client->poll_trigger->revents & POLLIN)
	{
		// printf("got to receive buf\n");
		chat_client_receive_buf(client);
	}
	return 0;
}

int
chat_client_get_descriptor(const struct chat_client *client)
{
	return client->socket;
}

int
chat_client_get_events(const struct chat_client *client)
{
	if(client->socket == -1)
	{
		return 0;
	}
	if((client->poll_trigger->events) & POLLOUT)
	{
		return (CHAT_EVENT_INPUT | CHAT_EVENT_OUTPUT);
	}
	return CHAT_EVENT_INPUT;
}

// Trim message from extra spaces in the front and back
uint32_t trim_message(char* msg, int msg_size)
{
	int n_zeroes_front = 0, n_zeroes_back = 0;
	for(int i = 0; i < msg_size; i++)
	{
		if(msg[i] == ' ')
		{
			n_zeroes_front ++;
		}
		else
		{
			break;
		}
	}
	if(n_zeroes_front)
	{
		// todo: do it in a faster way mbe?
		for(int i = 0; i < msg_size; i++)
		{
			msg[i - n_zeroes_front] = msg[i];
		}
	}
	for(int i = msg_size - 2; i >= 0; i--)
	{
		if(msg[i] == ' ')
		{
			n_zeroes_back ++;
		}
		else
		{
			break;
		}
	}
	if(n_zeroes_back)
	{
		msg[msg_size- n_zeroes_back -1] = '\n';
	}
	return msg_size - n_zeroes_front - n_zeroes_back;
}

int
chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
	if(client->socket == -1)
	{
		return CHAT_ERR_NOT_STARTED;
	}
	int target_size = msg_size + client->output_buf_size;
	if(target_size > client->output_buf_capacity)
	{
		client->output_buf_capacity = target_size;
		client->output_buf = realloc(client->output_buf, target_size * sizeof(char));
	}
	memcpy(client->output_buf+client->output_buf_size,
	 	msg, msg_size);
	int start = 0;
	for(int i = client->output_buf_size; i < target_size; i++)
	{
		if(client->output_buf[i] == '\n')
		{
			int sz = i - start + 1;
			struct chat_message* new_message = malloc(sizeof(struct chat_message));
			char* message_content = calloc(sz, sizeof(char));
			message_content = memcpy(message_content, client->output_buf+start, sz);
			sz = trim_message(message_content, sz);
			// add terminating char instead of '\n', will be reverted above.
			message_content[sz - 1] = '\0';
			new_message->data = message_content;
			new_message->author = NULL;
			client->messages_to_be_sent = realloc(
							client->messages_to_be_sent, (client->n_messages_to_be_sent + 1) * sizeof(struct chat_message*));
			client->messages_to_be_sent[client->n_messages_to_be_sent] = new_message;
			client->n_messages_to_be_sent += 1;
			start = i + 1;
		}
	}
	client->output_buf_size = (int)target_size;
	if(start)
	{
		client->poll_trigger->events |= POLLOUT;
		client->output_buf_size -= start;
		char* new_buf = calloc(client->output_buf_capacity - start, sizeof(char));
		client->output_buf_capacity -= start;
		memcpy(new_buf, client->output_buf+start, client->output_buf_size);
		free(client->output_buf);
		client->output_buf = new_buf;
	}
	return 0;
}
