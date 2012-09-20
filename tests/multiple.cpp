/*
 * multiple.cpp
 *
 * Makes many requests. Does not display responses, but
 * checks that they look acceptable. No timeout is used.
 */

#include <iostream>
#include <map>
#include <ev.h>
#include <evhttpclient.h>

using namespace std;

#define NUM_REQS_TO_MAKE (1000)
#define PRINT_INTERVAL (100)
#define EXPECTED_RESPONSE_CODE (200)
#define MIN_NUM_HEADERS (7)
#define MAX_NUM_HEADERS (8)

int num_responses = 0;
int num_requests = 0;

double min_latency = -1;
double avg_latency = 0;
double max_latency = -1;

/*
 * Got a response. Check for errors and update stats.
 */
void response_cb(ResponseInfo *response, void *requestData, void *clientData)
{
	if(response == NULL)
	{
		cout << "Error." << endl;
		exit(1);
	}

	if(response->timeout)
	{
		cout << "Timeout." << endl;
		exit(1);
	}
	
	if(response->code != EXPECTED_RESPONSE_CODE)
	{
		cout << "Non-200 response code (" << response->code << ")." << endl;
		exit(1);
	}
	
	if(response->headers.size() < MIN_NUM_HEADERS ||
		response->headers.size() > MAX_NUM_HEADERS)
	{
		cout << "Incorrect number of headers (was " << response->headers.size()
			<< ", should be between " << MIN_NUM_HEADERS << " and " 
			<< MAX_NUM_HEADERS << ")." << endl;
		cout << "Headers" << endl << "-----" << endl;
		for(map<string, string>::const_iterator iter = response->headers.begin();
			iter != response->headers.end(); ++iter)
		{
			cout << iter->first << ": " << iter->second << endl;
		}
		exit(1);
	}
	
	size_t contentLength = 0;
	stringstream ss;
	ss << response->headers["Content-Length"];
	ss >> contentLength;
	if(response->response.size() != contentLength)
	{
		cout << "Incorrect response length (was " << response->response.size()
			<< ", should be " << contentLength << ")." << endl;
		cout << "Response" << endl << "-----" << endl;
		cout << response->response << endl;
		exit(1);
	}
	
	num_responses++;

	// Compute stats
	if(response->latency < min_latency || min_latency < 0)
	{
		min_latency = response->latency;
	}
	
	if(response->latency > max_latency || max_latency < 0)
	{
		max_latency = response->latency;
	}
	
	avg_latency = (avg_latency * (num_responses - 1) + response->latency) / num_responses;
	
	if(num_responses % PRINT_INTERVAL == 0)
	{
		cout << num_responses << " responses." << endl;
	}

	if(num_responses == NUM_REQS_TO_MAKE)
	{
		cout << "Done." << endl;
		cout << "Min Latency: " << min_latency << endl;
		cout << "Avg Latency: " << avg_latency << endl;
		cout << "Max Latency: " << max_latency << endl;
		exit(0);
	}
}

/*
 * Timer goes off after 1ms. This is where we
 * make a request.
 */
void timer_cb(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	num_requests++;
	
	EvHttpClient *client = (EvHttpClient *) timer->data;
	
	int get = client->makeGet(response_cb, "", map<string, string>(), NULL);
	if(get < 0)
	{
		cout << "Error making request." << endl;
		exit(1);
	}
	
	if(num_requests == NUM_REQS_TO_MAKE)
	{
		ev_timer_stop(loop, timer);
	}
}

/* Main */
int main()
{
	// Initialize event loop.
	struct ev_loop *loop = ev_default_loop(0);
	
	// Initialize HTTP client.
	EvHttpClient client(loop, "http://www.greenhondacivicsunite.com/", 0, NULL);
	
	// Set up timer.
	struct ev_timer timer;
	timer.data = (void *) &client;
	ev_timer_init(&timer, timer_cb, 0.001, 0.001);
	ev_timer_start(loop, &timer);
	
	cout << "Starting loop." << endl;

	while (1)
	{
		ev_loop(loop, 0);
	}
	
	return 0;
}

