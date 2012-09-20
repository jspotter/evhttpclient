/***************************************************************
* EVHTTPCLIENT
* ------------
* Http client which uses the libev event loop.
*
* Currently not thread-safe.
*
*/

#ifndef EVHTTPCLIENT_H_
#define EVHTTPCLIENT_H_

#include <queue>
#include <map>
#include <string>
#include <iostream>
#include <sstream>
#include <arpa/inet.h>
#include <sys/time.h>
#include <ev.h>
#include "url.h"
#include "http_parser.h"

#define DEFAULT_BLOCK_SIZE (1024)
#define DEFAULT_INIT_NUM_CONNS (100)

using namespace std;

/* Forward decs */
class HttpConn;
class RequestInfo;

/*
 * Information about an HTTP response. Passed back to
 * user of EvHttpClient via a callback. If timeout is
 * true, this means the request timed out, and the other
 * fields of this class are more or less meaningless
 * EXCEPT latency.
 */
class ResponseInfo
{
	public:
		bool timeout;
		short code;
		double latency;
		map<string, string> headers;
		string response;
};

/*
 * Signature for the callback function that an
 * EvHttpClient requires.
 *
 * The first argument is a pointer to a ResponseInfo object.
 * If this is NULL, an error occurred.
 *
 * The second argument is the data pointer passed to the
 * request function (makeRequest, makeGet, etc.).
 *
 * The third argument is the data pointer passed to the
 * EvHttpClient constructor.
 */
typedef void (*EvHttpClientCallback) (ResponseInfo *, void *, void *);


/*
 * An HTTP client. Uses a pool of HttpConn objects
 * to make requests.
 */
class EvHttpClient
{
	friend class HttpConn;
	friend class RequestInfo;

	public:
		/*
		 * Constructor.
		 *
		 * The event loop must have already been initialized with
		 * something like:
		 *
		 * loop = ev_default_loop(0)
		 *
		 * It need not have been started.
		 *
		 * The URL need only include protocol, host, and port. It
		 * can optionally include a path and query string, which will
		 * then be used as the default path and query string for
		 * requests. Path and query string can also be specified
		 * on a per-request basis via the path parameter in the
		 * request functions (see below).
		 *
		 * The timeout parameter specifies a number of seconds
		 * before a request times out. Note that each request will
		 * timeout after AT LEAST this amount of time. In practice,
		 * this can vary. This timeout can be adjusted via
		 * setTimeout. If the timeout is 0, an infinite timeout
		 * will be used.
		 *
		 * The data pointer here will be passed as the third
		 * argument to the EvHttpClientCallback specified on
		 * each request.
		 *
		 * The init_num_conns parameter specifies how many
		 * connections this client's connection pool will
		 * start with.
		 *
		 * The block_size parameter specifies how many bytes
		 * this client tries to receive on each call to recv.
		 */
		EvHttpClient(struct ev_loop *loop, const string & url,
			double timeout, void *data, int init_num_conns = DEFAULT_INIT_NUM_CONNS,
			int block_size = DEFAULT_BLOCK_SIZE);
		~EvHttpClient();

		/*
		 * Set the timeout for each request.
		 *
		 * After this call, every timer that gets started within
		 * the EvHttpClient will use this timeout. Note: this does
		 * not mean that existing timers for pending requests will
		 * adjust their timeouts. Again, if the timeout is 0, an
		 * infinite timeout will be used.
		 */
		void setTimeout(double seconds);
	
		/*
		 * Request functions
		 *
		 * Make a request using an EvHttpClient object. The data pointer
		 * passed to a request function will be passed to the specified
		 * callback as the second argument.
		 * 
		 * Each version of this function takes a callback that conforms 
		 * to the EvHttpClientCallback signature. The first argument to this
		 * callback will be a ResponseInfo object, which will be freed
		 * automatically by the EvHttpClient after the callback returns.
		 * If the callback is NULL, a dummy callback that does nothing
		 * will be called.
		 *
		 * Specifying the path for a request is optional. If no path is 
		 * specified (if the path is of length 0), the path indicated by the 
		 * url given to the EvHttpClient constructor will be used.
		 *
		 * Each return 0 on success, -1 on failure.
		 *
		 */
		int makeRequest(EvHttpClientCallback cb,
			const string requestString, void *data);
		int makeRequest(EvHttpClientCallback cb, const string & path, 
			const string & method, const map<string, string> & headers, 
			const string & body, void *data);
		int makeGet(EvHttpClientCallback cb, const string & path,
			const map<string, string> & headers, void *data);
		int makePost(EvHttpClientCallback cb, const string & path, 
			const map<string, string> & headers,
			const string & body, void *data);
		int makePut(EvHttpClientCallback cb, const string & path, 
			const map<string, string> & headers,
			const string & body, void *data);
		int makeDelete(EvHttpClientCallback cb, const string & path,
			const map<string, string> & headers,
			const string & body, void *data);

	private:
		struct ev_loop *loop;

		http_parser_settings parser_settings;

		queue<HttpConn *> connections;

		double timeout;

		int init_num_conns;
		int block_size;

		Url url;
		int family;
		int socktype;
		int protocol;
		size_t addrlen;
		struct sockaddr addr;

		void *data;

		void retryRequest(RequestInfo *request);
		HttpConn *createConn();
		void destroyConn(HttpConn *conn);
		HttpConn *getConn();
		void returnConn(HttpConn *conn);
		void finalizeTimeout(RequestInfo *request);
		void finalizeError(RequestInfo *request);
		void writeCb(struct ev_loop *loop, struct ev_io *watcher, int revents);
		void readCb(struct ev_loop *loop, struct ev_io *watcher, int revents);
		void timeoutCb(struct ev_loop *loop, struct ev_timer *timer, int revents);
		void initConnPool();
		string buildRequest(const string & path, const string & method,
			const map<string, string> & headers, const string & body);

};

#endif /* EVHTTPCLIENT_H_ */

