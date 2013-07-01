#ifndef URL_H_
#define URL_H_    
#include <string>
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
using namespace std;

typedef struct Url_
{
	Url_()
	{
	}

	Url_(const string& url_s)
	{
		parse(url_s);
	}

	string protocol()
	{
		return protocol_;
	}

	string host()
	{
		return host_;
	}

	int port()
	{
		return port_;
	}

	string path()
	{
		return path_;
	}

	string query()
	{
		return query_;
	}
		
	void parse(const string& url_s)
	{
		const string prot_end("://");
		string::const_iterator prot_i = search(url_s.begin(), url_s.end(),
				                               prot_end.begin(), prot_end.end());
		protocol_.reserve(distance(url_s.begin(), prot_i));
		transform(url_s.begin(), prot_i,
					back_inserter(protocol_),
					ptr_fun<int,int>(tolower)); // protocol is icase
		if( prot_i == url_s.end() )
			return;
		
		advance(prot_i, prot_end.length());
		string::const_iterator port_i = find(prot_i, url_s.end(), ':');
		string::const_iterator path_i = min(find(prot_i, url_s.end(), '/'), find(prot_i, url_s.end(), '?'));
		string::const_iterator next_i = (port_i == url_s.end() ? path_i : port_i);
		if(port_i != url_s.end())
		{
			++port_i;
			std::stringstream portstream;
			while(port_i != path_i)
			{
				portstream << *port_i;
				++port_i;
			}
			portstream >> port_;
		}
		else
		{
			port_ = 80;
		}
	
		host_.reserve(distance(prot_i, next_i));
		transform(prot_i, next_i,
					back_inserter(host_),
					ptr_fun<int,int>(tolower)); // host is icase
		string::const_iterator query_i = find(path_i, url_s.end(), '?');
		path_.assign(path_i, query_i);
		if( query_i != url_s.end() )
		++query_i;
		query_.assign(query_i, url_s.end());
		if(path_.size() == 0)
		{
			path_ = "/";
		}
	}

	string toString()
	{
		stringstream ss;
		ss
			<< "protocol: " << protocol_ << endl
			<< "host: " << host_ << endl
			<< "port: " << port_ << endl
			<< "path: " << path_ << endl
			<< "query: " << query_ << endl;
		return ss.str();
	}
	
private:
    string protocol_, host_, path_, query_;
    short port_;
} Url;

#endif /* URL_H_ */
