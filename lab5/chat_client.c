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
#define BUF_LIMIT 10240
struct chat_client {
	/** Socket connected to the server. */
	int socket;
	/** Input buffer. */
	/*for incomplete messages*/
	char* input_buf;
	int input_buf_size;
	int input_buf_capacity;
	/** Array of received messages. */
	/*for complete messages*/
	int n_received_messages;
	struct chat_message** received_messages;
	/** Output buffer. */
	char* output_buf;
	int output_buf_size;
	int output_buf_capacity;
	int full_output_buf;
	/* PUT HERE OTHER MEMBERS */
	char* name;
	struct pollfd* poll_trigger;
	int is_name_sent;
	int some_name_sent;
};

struct chat_client *
chat_client_new(const char *name)
{

	struct chat_client* client = calloc(1, sizeof(struct chat_client));
	if (client == NULL)
	{
        perror("Error allocating memory for chat client\n");
        abort();
    }
	client->socket = -1;
	client->input_buf = NULL;
	client->input_buf_capacity = 0;
	client->input_buf_size = 0;
	client->n_received_messages = 0;
	client->received_messages = NULL;
	client->full_output_buf = 0;
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
	if(client->output_buf_capacity)
	{
		free(client->output_buf);
	}
	if(client->input_buf_capacity)
	{
		free(client->input_buf);
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
	char* msg = calloc(sz + 2, sizeof(char));
	memcpy(msg, client->name, sz);
	msg[sz] = '\n';
	msg[sz + 1] = '\0'; 
	sz ++;
	int send_counter = 0;
	while(1)
	{
		size_t send_size = sz - n_sent;
		if(send_size > BUF_LIMIT)
		{
			send_size = BUF_LIMIT;
		}
		if(send_counter++ == 10)
		{
			rc = -1;
			errno = EWOULDBLOCK;
		}
		else{
			rc = send(client->socket, msg+ n_sent, send_size, 0);
		}
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
	if( (errno == EAGAIN || errno == EWOULDBLOCK) && n_sent < sz)
	{
		client->some_name_sent = n_sent;
		return 0;
	}
	client->is_name_sent = 1;
	return 0;
}

int chat_client_send_buf(struct chat_client* client)
{
	if(!client->full_output_buf)
	{
		return 0;
	}
	int rc = 0;
	int total_sent = 0;
	int sz = client->full_output_buf;
	int send_counter = 0;
	while(sz)
	{
		size_t send_size = sz;
		if(send_size > BUF_LIMIT)
		{
			send_size = BUF_LIMIT;
		}
		if(send_counter++ == 10)
		{
			rc = -1;
			errno = EWOULDBLOCK;
		}
		else{
			rc = send(client->socket, client->output_buf+ total_sent, send_size, 0);
		}
		if(rc < 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				// Calculate the number of characters to keep
				if(total_sent)
				{
					client->output_buf_size -= (client->full_output_buf - sz);
					client->full_output_buf = sz;
					memmove(client->output_buf,
							client->output_buf + total_sent, client->output_buf_size);
					client->output_buf[sz] = '\0';
				}
			}
			return CHAT_ERR_SYS;
		}
		else
		if(rc == 0)
		{
			return CHAT_ERR_SYS;
		}
		else
		{
			sz -= rc;
			total_sent += rc;
		}
	}

	client->output_buf_size -= (client->full_output_buf - sz);
	client->full_output_buf = sz;
	memmove(client->output_buf,
			client->output_buf + total_sent, client->output_buf_size);
	client->output_buf[client->output_buf_size] = '\0';
	if(!client->full_output_buf)
	{
		client->poll_trigger->events = POLLIN;
	}
	return total_sent;
}

int chat_client_receive_buf(struct chat_client* client)
{
	int rc = 0, n_received = 0;
	if(client->input_buf_capacity == 0)
	{
		client->input_buf = calloc(BUF_SIZE, sizeof(char));
		client->input_buf_capacity = BUF_SIZE;
	}
	int recv_count = 0;
	do{
		client->input_buf_size += rc;
		n_received += rc;
		if(client->input_buf_size == client->input_buf_capacity)
		{
			client->input_buf_capacity *= 2;
			char* new_buf = calloc(client->input_buf_capacity, sizeof(char));
			memcpy(new_buf, client->input_buf, client->input_buf_size);
			free(client->input_buf);
			client->input_buf = new_buf;
		}
		int recv_size = client->input_buf_capacity - client->input_buf_size;
		if(recv_size > BUF_LIMIT)
		{
			recv_size = BUF_LIMIT;
		}
		if(recv_count++ == 10)
		{
			break;
		}
		else
		{
			rc = recv(client->socket, client->input_buf + client->input_buf_size,
						recv_size, MSG_DONTWAIT);
		}
	}while(rc > 0);
	if(n_received == 0)
	{
		return 0;
	}
	else
	{
		int start = 0;
		int author_end = 0;
		int is_first = 1;
		for(int i = 0; i < client->input_buf_size; i++)
		{
			// first time it's author, second it's message.
			if(client->input_buf[i] == '\n')
			{
				struct chat_message* new_message;
				if(is_first)
				{
					author_end = i;
					is_first = 0;
					continue;
				}
				new_message = calloc(1, sizeof(struct chat_message));
				client->received_messages = realloc(client->received_messages, (client->n_received_messages + 1) * sizeof(struct chat_message*));
				client->received_messages[client->n_received_messages] = new_message;
				client->n_received_messages += 1;
				new_message = client->received_messages[client->n_received_messages-1];
				
				if(author_end)
				{
					char* message_content = calloc(author_end - start + 1, sizeof(char));
					message_content = memcpy(message_content, client->input_buf+start, (author_end - start +1));
					// add terminating character instead of '\n'
					message_content[author_end-start] = '\0';
					new_message->author = message_content;
					start = author_end + 1;
				}
				if(start) // dummy cond
				{
					char* message_content = calloc(i - start + 1, sizeof(char));
					message_content = memcpy(message_content, client->input_buf+start, (i - start +1));
					// add terminating character instead of '\n'
					message_content[i-start] = '\0';
					new_message->data = message_content;
					is_first = 1;
				}
				start = i + 1;
			}
		}
		if(start == client->input_buf_size)
		{
			free(client->input_buf);
			client->input_buf = NULL;
			client->input_buf_capacity = 0;
			client->input_buf_size = 0;
		}
		else
		{
			if(start)
			{
				int remaining = client->input_buf_size - start;
				memmove(client->input_buf, client->input_buf + start, remaining);
				client->input_buf[remaining] = '\0';
				client->input_buf_size -= start;
			}
		}
	}
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
		if(client->is_name_sent)
		{
			chat_client_send_buf(client);
		}
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
		memcpy(msg, msg+n_zeroes_front, msg_size - n_zeroes_front);
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
	if(target_size >= client->output_buf_capacity)
	{
		client->output_buf_capacity = target_size+1;
		client->output_buf = realloc(client->output_buf, client->output_buf_capacity * sizeof(char));
	}
	memcpy(client->output_buf+client->output_buf_size,
	 	msg, msg_size);
	client->output_buf[target_size] = '\0';
	for(int i = client->output_buf_size; i < target_size; i++)
	{
		if(client->output_buf[i] == '\n')
		{
			client->full_output_buf = i+1;
		}
	}
	client->output_buf_size = (int)target_size;
	if(client->full_output_buf)
	{
		client->poll_trigger->events |= POLLOUT;
	}
	return 0;
}
