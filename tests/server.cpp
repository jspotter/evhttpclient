/*
 * server.cpp
 *
 * Implements an HTTP server that pings a number of 
 * remote hosts in response to a request, and responds
 * only when each request has received a response or
 * timed out.
 *
 * To run from the top directory, type
 *
 * $ tests/server [port]
 *
 * If you do not specify a port, it will default to
 * port 8000.
 *
 * When running, wait for it to print "Listening on 
 * port [port]." From another tab, type:
 *
 * $ curl http://127.0.0.1:[port]/
 *
 * This should result in 
 */

#include <strings.h>
#include <netinet/in.h>
#include <iostream>
#include <map>
#include <vector>
#include <stdio.h>
#include <ev.h>
#include <evhttpclient.h>

using namespace std;

#define MSG ("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\nHost: JP\r\n\r\nhello world\n")
#define TIMEOUT (0.3)

static string urls[] =
	{
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/",
		"http://greenhondacivicsunite.com/"
	};

static int num_urls = 10;

static int min_responses = -1;
static double avg_responses = 0;
static int max_responses = -1;
static int total_responses = 0;

static vector<EvHttpClient *> clients;

/* Struct to keep track of how many responses we have received. */
typedef struct ReqInfo_
{
	int fd;
	vector<string> responses;
	int num_responses;
	int expected_num_responses;
} ReqInfo;

/* Keep track of how many responses we have been receiving on
 * each request. */
void update_stats(int num_responses)
{
	total_responses++;
	if(num_responses < min_responses || min_responses == -1)
	{
		min_responses = num_responses;
	}
	if(num_responses > max_responses || max_responses == -1)
	{
		max_responses = num_responses;
	}
	avg_responses = (avg_responses * (total_responses - 1) + num_responses) / total_responses;

	if(total_responses % 1000 == 0)
	{
		cout << "Stats" << endl << "-----" << endl;
		cout << "  min: " << min_responses << endl;
		cout << "  avg: " << avg_responses << endl;
		cout << "  max: " << max_responses << endl;
		cout << "total: " << total_responses << endl << endl;
	}
}

/* Callback when we get a response. */
void response_callback(ResponseInfo *response, void *request_data,
	void *client_data)
{
	ReqInfo *req_info = (ReqInfo *) request_data;

	req_info->num_responses++;
	if(response == NULL)
	{
		cout << "Error." << endl;
		return;
	}

	if(!response->timeout)
	{
		req_info->responses.push_back(response->response);
	}

	if(req_info->num_responses == req_info->expected_num_responses)
	{
		int num_responses = req_info->responses.size();
		update_stats(num_responses);
		send(req_info->fd, MSG, sizeof MSG, 0);
		close(req_info->fd);
		delete req_info;
	}
}

/* Callback for when the socket corresponsding to our new
 * request becomes readable. This basically amounts to a new
 * request happening. */
static void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	ReqInfo *req_info = new ReqInfo();
	req_info->num_responses = 0;
	req_info->expected_num_responses = num_urls;
	req_info->fd = watcher->fd;
	for(int i = 0; i < num_urls; ++i)
	{
		clients[i]->makeGet(response_callback, "", map<string, string>(), req_info);
	}
}

/* Callback for when someone tries to connect to our server. */
static void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int client_sd;
	struct ev_io *w_client = (struct ev_io*) malloc (sizeof(struct ev_io));
	bzero(w_client, sizeof(struct ev_io));

	if(EV_ERROR & revents)
	{
		perror("got invalid event");
		return;
	}

	// Accept client request
	client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);

	if (client_sd < 0)
	{
		perror("accept error");
		return;
	}

	// Initialize and start watcher to read client requests
	ev_io_init(w_client, read_cb, client_sd, EV_READ);
	ev_io_start(loop, w_client);
}

/* Main */
int main(int argc, char **argv)
{
	unsigned short port = 8000;
	if(argc > 1)
	{
		sscanf(argv[1], "%hu", &port);
	}

	struct ev_loop *loop = ev_default_loop(0);

	// Initialize clients
	cout << "Initializing clients..." << endl;
	for(int i = 0; i < num_urls; ++i)
	{
		clients.push_back(new EvHttpClient(loop, urls[i], TIMEOUT, NULL, 10));
	}

	// Create server socket
	int sd;
	if( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("server socket error");
		return -1;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	// Bind socket to address
	if (bind(sd, (struct sockaddr*) &addr, sizeof(addr)) != 0)
	{
		perror("bind error");
	}

	// Start listing on the socket
	if (listen(sd, 2) < 0)
	{
		perror("listen error");
		return -1;
	}

	// Initialize and start a watcher to accepts client requests
	struct ev_io w_accept;
	ev_io_init(&w_accept, accept_cb, sd, EV_READ);
	ev_io_start(loop, &w_accept);

	cout << "Listening on port " << port << "." << endl;
	while(1)
	{
		ev_loop(loop, 0);
	}

	return 0;
}
