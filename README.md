# evhttpclient

An HTTP client in C++ that uses the [libev](http://software.schmorp.de/pkg/libev.html) event library.

### Features

* Makes HTTP requests asynchronously.
* Uses a connection pool.
* Allows the user to specify and dynamically adjust a timeout value for a single request.

### Installing libev

```
$ wget http://dist.schmorp.de/libev/Attic/libev-4.11.tar.gz
$ tar xzvf libev-4.11.tar.gz
$ cd libev-4.11
$ ./configure --prefix=/usr
$ make
$ sudo make install
```

### Usage

Usage examples can be found in the tests/ directory.

