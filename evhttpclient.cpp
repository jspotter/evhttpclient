#include <algorithm>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "evhttpclient.h"

/******************************
* Forward decs for callbacks. *
******************************/
static void writeCbWrapper(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void readCbWrapper(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void timeoutCbWrapper(struct ev_loop *loop, struct ev_timer *timer, int revents);
static int messageBeginCb(http_parser *parser);
static int headerFieldCb(http_parser *parser, const char *at, size_t len);
static int headerValueCb(http_parser *parser, const char *at, size_t len);
static int headersCompleteCb(http_parser *parser);
static int bodyCb(http_parser *parser, const char *at, size_t len);
static int messageCompleteCb(http_parser *parser);


/*********************
* Class declarations *
*********************/

/*
 * Information about an HTTP request. Created when
 * request is made.
 */
class RequestInfo
{
	public:
		ResponseInfo *response;
		HttpConn *conn;
		EvHttpClient *client;
		EvHttpClientCallback cb;
		string requestString;
		struct timeval start;
		struct ev_timer timer;
		void *data;
		
		RequestInfo(EvHttpClient *client);
		void timeoutCb(struct ev_loop *loop, struct ev_timer *timer, int revents);
};

/*
 * Encapsulates an HTTP client connection object. Includes
 * an HTTP parser and callbacks to build headers, response,
 * etc.
 */
class HttpConn
{
	public:
		enum HeaderState { HEADER_STATE_FIELD, HEADER_STATE_VALUE, HEADER_STATE_DONE };
	
		int fd;		
		RequestInfo *request;
		size_t requestBytesSent;
		
		http_parser parser;
		HeaderState headerState;
		string headerField;
		string headerValue;
		string body;
		
		struct ev_io writeWatcher;
		struct ev_io readWatcher;
		
		bool messageBegun;
		bool messageComplete;
		bool responseSent;
		bool isNew;

		HttpConn(EvHttpClient *client);
		void resetState();
		void writeCb(struct ev_loop *loop, struct ev_io *watcher, int revents);
		void readCb(struct ev_loop *loop, struct ev_io *watcher, int revents);
		int messageBeginCb(http_parser *parser);
		void flushHeaders();
		int headerFieldCb(http_parser *parser, const char *at, size_t len);
		int headerValueCb(http_parser *parser, const char *at, size_t len);
		int headersCompleteCb(http_parser *parser);
		int bodyCb(http_parser *parser, const char *at, size_t len);
		int messageCompleteCb(http_parser *parser);
		
	private:
		EvHttpClient *client;
};

/*******************
* Helper functions *
*******************/

/*
 * Calculate the difference between
 * two struct timevals in microseconds.
 */
static long difftime(struct timeval *end, struct timeval *start)
{
	long start_usec = start->tv_usec;
	long end_usec = end->tv_usec;

	start->tv_usec = 0;
	if(end->tv_usec < start_usec)
	{
		end->tv_sec--;
		end->tv_usec = 1000000 - start_usec + end_usec;
	}
	else
	{
		end->tv_usec -= start_usec;
	}

	return ((end->tv_sec - start->tv_sec) * 1000000) + end->tv_usec;
}


/************************
* EvHttpClient (public) *
************************/

/*
 * Constructor. Initialize parser, connection pool, etc.
 */
EvHttpClient::EvHttpClient(struct ev_loop *loop, const string & urlstring,
	double timeout, void *data, int init_num_conns, int block_size)
{
	// Initialize loop, timeout, and url //TODO connect and request timeouts
	this->loop = loop;
	this->timeout = timeout;
	url.parse(urlstring);
	this->data = data;
	this->init_num_conns = init_num_conns;
	this->block_size = block_size;
	
	// Initialize addr
	char *host = strdup(url.host().c_str());
	stringstream ss;
	ss << url.port();
	char *service = strdup(ss.str().c_str());
	struct addrinfo *info = NULL;
	struct addrinfo hints;
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	int err = getaddrinfo(host, service, &hints, &info);
	if(err < 0)
	{
		cout << "Error: couldn't resolve host name " << this << endl;
		free(host);
		free(service);
		return;
	}

	family = info->ai_family;
	socktype = info->ai_socktype;
	protocol = info->ai_protocol;
	addrlen = info->ai_addrlen;
	memcpy(&addr, info->ai_addr, addrlen);

	free(host);
	free(service);
	freeaddrinfo(info);
	
	// Initialize parser settings
	parser_settings.on_message_begin = messageBeginCb;
	parser_settings.on_url = NULL;
	parser_settings.on_header_field = headerFieldCb;
	parser_settings.on_header_value = headerValueCb;
	parser_settings.on_headers_complete = headersCompleteCb;
	parser_settings.on_body = bodyCb;
	parser_settings.on_message_complete = messageCompleteCb;
	
	// Initialize connection pool
	initConnPool();
}

/*
 * Destructor. Clean up connection pool.
 */
EvHttpClient::~EvHttpClient()
{
	while(!connections.empty())
	{
		HttpConn *conn = connections.front();
		connections.pop();
		delete conn;
	}
}

/*
 * Change timeout value for future requests. Does not affect
 * requests that are currently pending.
 */
void EvHttpClient::setTimeout(double seconds)
{
	timeout = seconds;
}

/*
 * Callback that does nothing for situations where the
 * user does not specify a callback (for fire-and-forget
 * situations).
 */
static void noOpCb(ResponseInfo *response, void *requestData,
	void *clientData)
{
	// Do nothing.
}

/*
 * Makes a request given a string. Starts event loop
 * structures.
 */
int EvHttpClient::makeRequest(EvHttpClientCallback cb,
	const string requestString, void *data)
{
	HttpConn *conn = getConn();
	if(conn == NULL)
	{
		return -1;
	}
	
	RequestInfo *request = new RequestInfo(this);
	request->response = new ResponseInfo();
	request->conn = conn;
	if(cb == NULL)
	{
		request->cb = noOpCb;
	}
	else
	{
		request->cb = cb;
	}
	request->cb = cb;
	request->requestString = requestString;
	gettimeofday(&request->start, NULL);
	ev_timer_init(&request->timer, timeoutCbWrapper, timeout, 0.);
	request->timer.data = (void *) request;
	request->data = data;
	
	conn->request = request;
	ev_io_start(loop, &conn->writeWatcher);
	
	if(timeout > 0)
	{
		ev_timer_start(loop, &request->timer);
	}

	return 0;
}

/*
 * Makes a request given a method, set of headers, and
 * optional body.
 */
int EvHttpClient::makeRequest(EvHttpClientCallback cb,
	const string & path, const string & method,
	const map<string, string> & headers,
	const string & body, void *data)
{
	string request = buildRequest(path, method, headers, body);
	return makeRequest(cb, request, data);
}

/*
 * Makes GET.
 */
int EvHttpClient::makeGet(EvHttpClientCallback cb, const string & path,
	const map<string, string> & headers, void *data)
{
	return makeRequest(cb, path, "GET", headers, "", data);
}

/*
 * Makes POST.
 */
int EvHttpClient::makePost(EvHttpClientCallback cb, const string & path,
	const map<string, string> & headers, const string & body,
	void *data)
{
	return makeRequest(cb, path, "POST", headers, body, data);
}

/*
 * Makes PUT.
 */
int EvHttpClient::makePut(EvHttpClientCallback cb, const string & path,
	const map<string, string> & headers, const string & body,
	void *data)
{
	return makeRequest(cb, path, "PUT", headers, body, data);
}

/*
 * Makes DELETE.
 */
int EvHttpClient::makeDelete(EvHttpClientCallback cb, const string & path,
	const map<string, string> & headers, const string & body,
	void *data)
{
	return makeRequest(cb, path, "DELETE", headers, body, data);
}


/*************************
* EvHttpClient (private) *
**************************/

/*
 * Retries a request given a request object.
 */
void EvHttpClient::retryRequest(RequestInfo *request)
{
	if(request->conn != NULL)
	{
		destroyConn(request->conn);
		request->conn = NULL;
	}

	request->conn = getConn();
	if(request->conn == NULL)
	{
		finalizeError(request);
		return;
	}
	
	request->conn->request = request;
	ev_io_start(loop, &request->conn->writeWatcher);
}

/*
 * Connects to the remote server. Returns the
 * new connection, or NULL on error.
 */
HttpConn *EvHttpClient::createConn()
{
	HttpConn *conn = new HttpConn(this);
	
	if( (conn->fd = socket(family, socktype, 
		protocol)) < 0)
	{   
		perror("socket error");
		delete conn;
		return NULL;
	}
	
	int flags = fcntl(conn->fd, F_GETFL, 0);
	fcntl(conn->fd, F_SETFL, flags | O_NONBLOCK);   

	if( (connect(conn->fd, &addr, addrlen)) < 0)
	{
		if(errno != EINPROGRESS)
		{
			perror("connect() failed");
			delete conn;
			return NULL;
		}
	}

	ev_io_init(&conn->writeWatcher, writeCbWrapper, conn->fd, EV_WRITE);
	ev_io_init(&conn->readWatcher, readCbWrapper, conn->fd, EV_READ);
	conn->writeWatcher.data = (void *) conn;
	conn->readWatcher.data = (void *) conn;
	
	conn->request = NULL;
	
	return conn;
}

/*
 * Disconnects and frees a connection object.
 *
 * IS NOT responsible for the connection's RequestInfo.
 * This must be freed prior to calling destroyConn.
 */
void EvHttpClient::destroyConn(HttpConn *conn)
{
	shutdown(conn->fd, 2);
	close(conn->fd);
	
	ev_io_stop(loop, &conn->writeWatcher);
	ev_io_stop(loop, &conn->readWatcher);
	
	delete conn;
}

/*
 * Retrieves a connection from the connection pool,
 * or creates a new one if the pool is empty.
 */
HttpConn *EvHttpClient::getConn()
{
	HttpConn *conn;
	
	if(connections.empty())
	{
		conn = createConn();
		if(conn == NULL)
		{
			return NULL;
		}
	}
	else
	{
		conn = connections.front();
		connections.pop();
		conn->resetState();
	}
	
	return conn;
}

/*
 * Returns a connection to the connection pool.
 *
 * IS NOT responsible for the connection's RequestInfo.
 */
void EvHttpClient::returnConn(HttpConn *conn)
{
	ev_io_stop(loop, &conn->writeWatcher);
	ev_io_stop(loop, &conn->readWatcher);

	connections.push(conn);
}

/*
 * Calls user callback on timeout.
 */
void EvHttpClient::finalizeTimeout(RequestInfo *request)
{
	ev_timer_stop(loop, &request->timer);
	ResponseInfo *response = request->response;
	
	response->timeout = true;
	response->code = 0;
	response->latency = 0;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long diff = difftime(&tv, &request->start);
	request->response->latency = diff / 1000000.;
	request->cb(response, request->data, request->client->data);
	delete response;
	delete request;
}

/*
 * Calls user callback on error.
 */
void EvHttpClient::finalizeError(RequestInfo *request)
{
	ev_timer_stop(loop, &request->timer);
	request->cb(NULL, request->data, request->client->data);
	delete request->response;
	delete request;
}

/*
 * Internal callback for when a socket becomes writable.
 */
void EvHttpClient::writeCb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	if(EV_ERROR & revents)
	{
		perror("EvHttpClient got invalid write event");
		return;
	}

	HttpConn *conn = (HttpConn *) watcher->data;
	RequestInfo *request = conn->request;
	
	string toWrite = request->requestString.substr(conn->requestBytesSent);
	char *toWriteC = strdup(toWrite.c_str());
	
	int sent = send(watcher->fd, toWriteC, strlen(toWriteC), 0); //MSG_NOSIGNAL);
	free(toWriteC);
	
	if(sent < 0)
	{
		if(conn->isNew)
		{
			cout << "Write error" << endl;
			destroyConn(conn);
			request->conn = NULL;
			finalizeError(request);
		}
		else
		{
			destroyConn(conn);
			request->conn = NULL;
			retryRequest(request);
		}
		return;
	}
	
	conn->requestBytesSent += sent;
	if(conn->requestBytesSent == request->requestString.size())
	{
		ev_io_stop(loop, watcher);
		ev_io_start(loop, &conn->readWatcher);
	}
}

/*
 * Internal callback for when a socket becomes readable.
 */
void EvHttpClient::readCb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	if(EV_ERROR & revents)
	{
		perror("EvHttpClient got invalid read event");
		return;
	}
	
	char buffer[block_size];
	bzero(buffer, sizeof(buffer));
	HttpConn *conn = (HttpConn *) watcher->data;
	RequestInfo *request = conn->request;
	
	int received = recv(watcher->fd, buffer, sizeof(buffer), 0);
	if(received <= 0)
	{
		if(conn->isNew)
		{
			cout << "Read error " << received << endl;
			destroyConn(conn);
			request->conn = NULL;
			finalizeError(request);
		}
		else
		{
			destroyConn(conn);
			request->conn = NULL;
			retryRequest(request);
		}
		return;
	}
	
	buffer[received] = 0;
	http_parser_execute(&conn->parser, &parser_settings,
		buffer, received);
}

/*
 * Internal callback for when timer expires.
 */
void EvHttpClient::timeoutCb(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	RequestInfo *request = (RequestInfo *) timer->data;
	HttpConn *conn = request->conn;

	//ev_timer_stop(loop, timer); //handled in finalizeTimeout
	if(conn != NULL)
	{
		ev_io_stop(loop, &conn->writeWatcher);
		ev_io_stop(loop, &conn->readWatcher);
		http_parser_pause(&conn->parser, 1);
		destroyConn(conn);
		request->conn = NULL;
	}
	
	finalizeTimeout(request);
}

/*
 * Populates connection pool.
 */
void EvHttpClient::initConnPool()
{
	for(int i = 0; i < init_num_conns; ++i)
	{
		HttpConn *newConn = createConn();
		if(newConn == NULL)
		{
			break;
		}
		connections.push(newConn);
	}
}

/*
 * Builds a request string out of the method,
 * headers, and body.
 */
string EvHttpClient::buildRequest(const string & path, 
	const string & method, const map<string, string> & headers, 
	const string & body)
{
	stringstream request;
	string realMethod(method);
	transform(realMethod.begin(), realMethod.end(), realMethod.begin(), ::toupper);
	string realPath;
	if(path.size() == 0)
	{
		realPath = url.path() + "?" + url.query();
	}
	else
	{
		realPath = path;
	}
	request << realMethod << " " << realPath << " HTTP/1.1\r\n";
	
	for(map<string, string>::const_iterator iter = headers.begin(); iter != headers.end(); ++iter)
	{
		request << iter->first << ": " << iter->second << "\r\n";
	}
	
	if(headers.find("Host") == headers.end())
	{
		request << "Host: " << url.host();
		short port = url.port();
		if(port != 80 && port != 443)
		{
			request << ":" << port;
		}
		request << "\r\n";
	}
	
	if(body.size() > 0 && headers.find("Content-Length") == headers.end())
	{
		request << "Content-Length: " << body.size() << "\r\n";
	}
	
	request << "\r\n" << body;

  return request.str();
}


/***********************
* RequestInfo (public) *
***********************/

/*
 * Constructor assigns HttpClient to this
 * request object.
 */
RequestInfo::RequestInfo(EvHttpClient *client)
{
	this->client = client;
}

/*
 * Timeout callback defers to client.
 */
void RequestInfo::timeoutCb(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	client->timeoutCb(loop, timer, revents);
}


/*********************
* HttpConn (public)  *
*********************/

/*
 * Constructor assigns client to this
 * connection object and initializes other
 * data.
 */
HttpConn::HttpConn(EvHttpClient *client)
{
	this->client = client;
	
	request = NULL;
	
	requestBytesSent = 0;

	headerField = "";
	headerValue = "";
	body = "";

	http_parser_init(&parser, HTTP_RESPONSE);
	parser.data = (void *) this;
	headerState = HEADER_STATE_FIELD;

	messageBegun = false;
	messageComplete = false;
	responseSent = false;
	isNew = true;
}

/*
 * Resetter (doesn't reset fd, lastReq, watchers,
 * timer, or cb)
 */
void HttpConn::resetState()
{
	if(request != NULL)
	{
		delete request->response;
	}
	delete request;
	request = NULL;

	requestBytesSent = 0;
	
	http_parser_init(&parser, HTTP_RESPONSE);
	parser.data = (void *) this;
	
	headerState = HEADER_STATE_FIELD;
	
	headerField = "";
	headerValue = "";
	body = "";
	
	messageBegun = false;
	messageComplete = false;
	responseSent = false;
	isNew = false;
}

/*
 * Write callback defers to client's callback.
 */
void HttpConn::writeCb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	client->writeCb(loop, watcher, revents);
}

/*
 * Read callback defers to client's callback.
 */
void HttpConn::readCb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	client->readCb(loop, watcher, revents);
}

/*
 * Callbacks for the HTTP parser.
 */
int HttpConn::messageBeginCb(http_parser *parser)
{
	if(messageBegun)
	{
		cout << "New message beginning prematurely." << endl;
		return 1;
	}
	
	if(request == NULL)
	{
		cout << "New message when request was NULL." << endl;
		return 1;
	}
	
	messageBegun = true;
	return 0;
}

void HttpConn::flushHeaders()
{
	request->response->headers[headerField] = headerValue;
	headerField = "";
	headerValue = "";
}

int HttpConn::headerFieldCb(http_parser *parser, const char *at, size_t len)
{
	char buffer[len + 1];
	strncpy(buffer, at, len);
	buffer[len] = 0;
	
	if(headerState == HEADER_STATE_DONE)
	{
		cout << "Header field received when headers were done." << endl;
		return 1;
	}
	
	if(headerState == HEADER_STATE_VALUE)
	{
		flushHeaders();
		headerState = HEADER_STATE_FIELD;
	}
	
	headerField += buffer;
	return 0;
}

int HttpConn::headerValueCb(http_parser *parser, const char *at, size_t len)
{
	char buffer[len + 1];
	strncpy(buffer, at, len);
	buffer[len] = 0;
	
	if(headerState == HEADER_STATE_DONE)
	{
		cout << "Header value received when headers were done." << endl;
		return 1;
	}
	
	headerState = HEADER_STATE_VALUE;
	headerValue += buffer;
	return 0;
}

int HttpConn::headersCompleteCb(http_parser *parser)
{
	headerState = HEADER_STATE_DONE;
	flushHeaders();
	return 0;
}

int HttpConn::bodyCb(http_parser *parser, const char *at, size_t len)
{
	char buffer[len + 1];
	strncpy(buffer, at, len);
	buffer[len] = 0;
	body += buffer;
	return 0;
}

int HttpConn::messageCompleteCb(http_parser *parser)
{
	ev_timer_stop(client->loop, &request->timer);

	messageComplete = true;
	request->response->response = body;

	request->response->timeout = false;
	request->response->code = parser->status_code;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	long diff = difftime(&tv, &request->start);
	request->response->latency = diff / 1000000.;

	request->cb(request->response, request->data, request->client->data);
	responseSent = true;
	
	delete request->response;
	delete request;
	request = NULL;
	
	client->returnConn(this);
	
	return 0;
}


/*************
* Callbacks  *
*************/
static void writeCbWrapper(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	HttpConn *conn = (HttpConn *) watcher->data;
	conn->writeCb(loop, watcher, revents);
}

static void readCbWrapper(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	HttpConn *conn = (HttpConn *) watcher->data;
	conn->readCb(loop, watcher, revents);
}

static void timeoutCbWrapper(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	RequestInfo *request = (RequestInfo *) timer->data;
	request->timeoutCb(loop, timer, revents);
}

static int messageBeginCb(http_parser *parser)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->messageBeginCb(parser);
}

static int headerFieldCb(http_parser *parser, const char *at, size_t len)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->headerFieldCb(parser, at, len);
}

static int headerValueCb(http_parser *parser, const char *at, size_t len)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->headerValueCb(parser, at, len);
}

static int headersCompleteCb(http_parser *parser)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->headersCompleteCb(parser);
}

static int bodyCb(http_parser *parser, const char *at, size_t len)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->bodyCb(parser, at, len);
}

static int messageCompleteCb(http_parser *parser)
{
	HttpConn *conn = (HttpConn *) parser->data;
	return conn->messageCompleteCb(parser);
}

