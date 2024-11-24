#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <deque>
#include <queue>

#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HttpService.h"
#include "HttpUtils.h"
#include "FileService.h"
#include "MySocket.h"
#include "MyServerSocket.h"
#include "dthread.h"

using namespace std;

int PORT = 8080;
int THREAD_POOL_SIZE = 1;
int BUFFER_SIZE = 1;
string BASEDIR = "static";
string SCHEDALG = "FIFO";
string LOGFILE = "/dev/null";

pthread_mutex_t request_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t add_request_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t execute_request_cond = PTHREAD_COND_INITIALIZER;

queue<MySocket*> request_buffer;
vector<pthread_t> request_threads;

vector<HttpService *> services;

HttpService *find_service(HTTPRequest *request) {
   // find a service that is registered for this path prefix
  for (unsigned int idx = 0; idx < services.size(); idx++) {
    if (request->getPath().find(services[idx]->pathPrefix()) == 0) {
      return services[idx];
    }
  }

  return NULL;
}


void invoke_service_method(HttpService *service, HTTPRequest *request, HTTPResponse *response) {
  stringstream payload;

  // invoke the service if we found one
  if (service == NULL) {
    // not found status
    response->setStatus(404);
  } else if (request->isHead()) {
    service->head(request, response);
  } else if (request->isGet()) {
    service->get(request, response);
  } else {
    // The server doesn't know about this method
    response->setStatus(501);
  }
}

void handle_request(MySocket *client) {
  HTTPRequest *request = new HTTPRequest(client, PORT);
  HTTPResponse *response = new HTTPResponse();
  stringstream payload;

  // read in the request
  bool readResult = false;
  try {
    payload << "client: " << (void *) client;
    sync_print("read_request_enter", payload.str());
    readResult = request->readRequest();
    sync_print("read_request_return", payload.str());
  } catch (...) {
    // swallow it
  }    
    
  if (!readResult) {
    // there was a problem reading in the request, bail
    delete response;
    delete request;
    sync_print("read_request_error", payload.str());
    return;
  }
  
  HttpService *service = find_service(request);
  invoke_service_method(service, request, response);

  // send data back to the client and clean up
  payload.str(""); payload.clear();
  payload << " RESPONSE " << response->getStatus() << " client: " << (void *) client;
  sync_print("write_response", payload.str());
  cout << payload.str() << endl;
  client->write(response->response());

  delete response;
  delete request;

  payload.str(""); payload.clear();
  payload << " client: " << (void *) client;
  sync_print("close_connection", payload.str());
  client->close();
  delete client;
}

// Main thread
void add_request(MySocket *client) {
  dthread_mutex_lock(&request_lock);

  while (request_buffer.size() >= (size_t)BUFFER_SIZE) {
    dthread_cond_wait(&add_request_cond, &request_lock);
  }

  request_buffer.push(client);
  dthread_cond_broadcast(&execute_request_cond);

  dthread_mutex_unlock(&request_lock);
}

// Worker thread
void* execute_request(void*) {
  while (true) {
    dthread_mutex_lock(&request_lock);

    while (request_buffer.size() == (size_t)0) {
      dthread_cond_wait(&execute_request_cond, &request_lock);
    }
    
    MySocket *client = request_buffer.front();
    request_buffer.pop();
    dthread_cond_signal(&add_request_cond);

    dthread_mutex_unlock(&request_lock);
    handle_request(client);
  }
  return NULL;
}

int main(int argc, char *argv[]) {

  signal(SIGPIPE, SIG_IGN);
  int option;

  while ((option = getopt(argc, argv, "d:p:t:b:s:l:")) != -1) {
    switch (option) {
    case 'd':
      BASEDIR = string(optarg);
      break;
    case 'p':
      PORT = atoi(optarg);
      break;
    case 't':
      THREAD_POOL_SIZE = atoi(optarg);
      break;
    case 'b':
      BUFFER_SIZE = atoi(optarg);
      break;
    case 's':
      SCHEDALG = string(optarg);
      break;
    case 'l':
      LOGFILE = string(optarg);
      break;
    default:
      cerr<< "usage: " << argv[0] << " [-p port] [-t threads] [-b buffers]" << endl;
      exit(1);
    }
  }

  set_log_file(LOGFILE);

  sync_print("init", "");
  MyServerSocket *server = new MyServerSocket(PORT);
  MySocket *client;

  // The order that you push services dictates the search order
  // for path prefix matching
  services.push_back(new FileService(BASEDIR));
  
  // Create threads
  request_threads.resize(THREAD_POOL_SIZE);
  for (int idx = 0; idx < THREAD_POOL_SIZE; idx++) {
    dthread_create(&request_threads[idx], NULL, execute_request, NULL);
  }

  while(true) {
    sync_print("waiting_to_accept", "");
    client = server->accept();
    sync_print("client_accepted", "");
    add_request(client);
  }
}
