/*
 * basic.cpp
 *
 * Makes a single request via an EvHttpClient object,
 * and displays the results. No timeout is used.
 */

#include <iostream>
#include <map>
#include <ev.h>
#include <evhttpclient.h>

using namespace std;

/*
 * Got a response. Display results.
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
		cout << "Timeout." << endl << endl;
		exit(1);
	}

	cout << "Code" << endl << "-----" << endl << response->code << endl << endl;
	cout << "Latency" << endl << "-----" << endl << response->latency << endl << endl;
	cout << "Headers" << endl << "-----" << endl;
	for(map<string, string>::const_iterator iter = response->headers.begin();
		iter != response->headers.end(); ++iter)
	{
		cout << iter->first << ": " << iter->second << endl;
	}
	cout << endl;
	cout << "Response" << endl << "-----" << endl;
	cout << response->response << endl;
		
	exit(0);
}

/*
 * Timer goes off after 1ms. This is where we
 * make a request.
 */
void timer_cb(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	cout << "Making GET." << endl;
	EvHttpClient *client = (EvHttpClient *) timer->data;
	int get = client->makeGet(response_cb, "", map<string, string>(), NULL);
	if(get < 0)
	{
		cout << "Error making request." << endl;
		exit(1);
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
	ev_timer_init(&timer, timer_cb, 0.001, 0.);
	ev_timer_start(loop, &timer);

	while (1)
	{
		ev_loop(loop, 0);
	}
	
	return 0;
}

