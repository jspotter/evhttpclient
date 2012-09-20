/*
 * server.cpp
 *
 * Implements an HTTP server that pings a number of 
 * remote hosts in response to a request, and responds
 * only when each request has received a response or
 * timed out.
 */

#include <iostream>
#include <map>
#include <vector>
#include <stdio.h>
#include <ev.h>
#include <ebb.h>
#include <evhttpclient.h>

using namespace std;

#define MSG ("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\nHost: JP\r\n\r\nhello world\n")
#define TIMEOUT (0.3)

static string urls[] =
	{
		"http://23.20.11.64?bidder=1",
		"http://23.20.11.64?bidder=2",
		"http://23.20.11.64?bidder=3",
		"http://23.20.11.64?bidder=4",
		"http://23.20.11.64?bidder=5",
		"http://23.20.11.64?bidder=6",
		"http://23.20.11.64?bidder=7",
		"http://23.20.11.64?bidder=8",
		"http://23.20.11.64?bidder=9",
		"http://23.20.11.64?bidder=10"
	};

static int num_urls = 10;

static int min_responses = -1;
static double avg_responses = 0;
static int max_responses = -1;
static int total_responses = 0;

static vector<EvHttpClient *> clients;

typedef struct ReqInfo_
{
	ebb_connection *connection;
	vector<string> responses;
	int num_responses;
	int expected_num_responses;
} ReqInfo;

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

static void response_done(ebb_connection *connection)
{
	ebb_connection_schedule_close(connection);
}

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
  	ebb_connection_write(req_info->connection, MSG, sizeof MSG, response_done);
		delete req_info;
	}
}

void on_close(ebb_connection *connection)
{
  free(connection);
}

static void request_complete(ebb_request *request)
{
  ebb_connection *connection = (ebb_connection *) request->data;

	ReqInfo *req_info = new ReqInfo();
	req_info->num_responses = 0;
	req_info->expected_num_responses = num_urls;
	req_info->connection = connection;
	for(int i = 0; i < num_urls; ++i)
	{
		clients[i]->makeGet(response_callback, "", map<string, string>(), req_info);
	}

  free(request);
}

static ebb_request* new_request(ebb_connection *connection)
{
  ebb_request *request = (ebb_request *) malloc(sizeof(ebb_request));
  ebb_request_init(request);
  request->data = connection;
  request->on_complete = request_complete;
  return request;
}

ebb_connection* new_connection(ebb_server *server, struct sockaddr_in *addr)
{
  ebb_connection *connection = (ebb_connection *) malloc(sizeof(ebb_connection));
  if(connection == NULL) {
    return NULL;
  }

  ebb_connection_init(connection);
  connection->data = NULL;
  connection->new_request = new_request;
  connection->on_close = on_close;
  
  return connection;
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
  ebb_server server;

  ebb_server_init(&server, loop);
  server.new_connection = new_connection;

	for(int i = 0; i < num_urls; ++i)
	{
		clients.push_back(new EvHttpClient(loop, urls[i], TIMEOUT, NULL));
	}

  cout << "Listening on port " << port << "." << endl;
  ebb_server_listen_on_port(&server, port);
  ev_loop(loop, 0);

	return 0;
}
