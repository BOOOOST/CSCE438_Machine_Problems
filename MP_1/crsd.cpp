#include <cstdlib>
#include <string>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include <glog/logging.h>
#include "interface.h"

// structure for storing information about a chat room
struct chatRoom {
	int pid, port, num_mem;	//process ID, port number
	std::string name;	//chat room name
	
	//constructor
	chatRoom(int id, int p, std::string n) {
		pid = id;
		port = p;
		name = n;
        num_mem = 0;
	}
};

struct sockaddr_in server, client;	//client and server information
std::vector<chatRoom> rooms;	//stores a list of all active chat rooms
int roomPort = 1876;	//new chatroom ports start at this number

// Initialize Functions
int cmdHandler(int i, fd_set *fds, int sockfd, int max_fds, Reply &rep);
int createRoom(std::string name, int n);
int deleteRoom(std::string name, int n);
int joinRoom(std::string name, int n, int i, Reply &rep);
void childProcess(int port);
void msgHandler(int i, fd_set *fds, int sockfd, int max_fds);

//handle reading/writing from/to client sockets

int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);

    LOG(INFO) << "Starting Server";
	fd_set fds, rfds;	//file descriptor sets
	int max_fds, port, acceptfd, sockfd;
	socklen_t length;
	rooms.clear();
	
	port = 8080;
	
	//server startup message
	printf("--------------------------------------\n");
	printf("Chat Service Master Server\n");
	printf("Usage: CREATE <string>\n");
	printf("       DELETE <string>\n");
	printf("       JOIN <string>\n");
	printf("       LIST\n");
	printf("--------------------------------------\n\n");
	
	//create and check master socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		LOG(INFO) << "ERROR in main, failed to create socket";
		exit(1);
	}
	
	//set sockaddr_in fields for server
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = INADDR_ANY;	//INADDR_ANY gets the IP of server machine
	memset(server.sin_zero, '\0', sizeof(server.sin_zero));
	
	//make address reuseable
	int yes = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		LOG(INFO) << "ERROR in main, failed to setsockopt";
		exit(1);
	}
	
	//bind socket to address
	if(bind(sockfd, (struct sockaddr *) &server, sizeof(struct sockaddr)) == -1) {
		LOG(INFO) << "ERROR in main, failed to bind socket";
		exit(1);
	}
	
	//listen for client connections
	listen(sockfd, 5);	//second argument is queue size
	printf("listening...\n\n");
	
	fflush(stdout);
	FD_ZERO(&fds);
	FD_ZERO(&rfds);
	FD_SET(sockfd, &fds);
	max_fds = sockfd;
    int stat;
	//infinitely loop waiting for connections and messages
	while(true) {
		rfds = fds;
		
		//monitor socket file descriptors
		if(select(max_fds+1, &rfds, NULL, NULL, NULL)  < 0) {
			LOG(ERROR) << "ERROR, in main: fd select failed";
		}
		
		//iterate through file descriptors checking for connections/messages
		for(int i=0; i<=max_fds; i++) {
			if(FD_ISSET(i, &rfds)) {	//checks if filedescriptor is in read set
				if(i == sockfd) {	//if i is an incoming connection, accept it
					acceptfd = 0;
					length = sizeof(struct sockaddr_in);
					
					//accept incoming connection
					acceptfd = accept(sockfd, (struct sockaddr *) &client, &length);
					if(acceptfd < 0){
						LOG(ERROR) << "ERROR, in main: accept connection failed\n";
					}
					else {
						FD_SET(acceptfd, &fds);
						if(acceptfd > max_fds) {
							max_fds = acceptfd;
						}
					}
				}
				else {
                    Reply rep;
					stat = cmdHandler(i, &fds, sockfd, max_fds, rep);
                    switch (stat){
                        case 0:
                            rep.status = SUCCESS;
                            break;
                        case 1:
                            rep.status = FAILURE_ALREADY_EXISTS;
                            break;
                        case 2:
                            rep.status = FAILURE_NOT_EXISTS;
                            break;
                        case 3:
                            rep.status = FAILURE_INVALID;
                            break;
                        case 4:
                            rep.status = FAILURE_UNKNOWN;
                            break;
                    }
					send(i, &rep, sizeof(rep),0);
				}
			}
		}
	}
	
	return 0;
}

// This function handles messages for child processes that has been created for chatrooms
// It deals with sending messages to all client connected to a room
void msgHandler(int i, fd_set *fds, int sockfd, int max_fds) {
	int n;	//number of chars sent/recieved
	char readBuf[256], writeBuf[256];
	
	//read from socket
	if((n = read(i, readBuf, sizeof(readBuf))) <= 0) {
		if(n == 0) {	//if client has disconnected from the server
			LOG(INFO) << "client " << i << " left\n";
		}
		else {
			LOG(ERROR) << "ERROR msgHandler, couldn't read message\n";	//read failed
		}
		close(i);
		FD_CLR(i, fds);
	}
	else {	//write to socket
		for(int j=0; j<=max_fds; j++) {
			if(FD_ISSET(j, fds)) {
				if(j != sockfd && j != i) {
					if(write(j, readBuf, n)  < 0) {
						LOG(ERROR) << "ERROR msgHandler, Couldn't write from chatroom to other clients\n";
					}
				}
			}
		}
	}
}

// Waits for connections and messages from clients and then 
// accepts or sends the message to the handler
void childProcess(int port) {
	fd_set fds, rfds;	//file descriptor sets
	int max_fds, acceptfd, sockfd;
	socklen_t length;
	struct sockaddr_in server, client;
	
	//create and check socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		LOG(ERROR) << "ERROR childProcess, failed to create socket\n";
		exit(1);
	}
	
	//set sockaddr_in fields for server
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = INADDR_ANY;	//INADDR_ANY gets the IP of server machine
	memset(server.sin_zero, '\0', sizeof(server.sin_zero));
	
	//make address reuseable
	int yes = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		LOG(ERROR) << "ERROR childProcess, failed to setsockopt\n";
		exit(1);
	}
	
	//bind socket to address
	if(bind(sockfd, (struct sockaddr *) &server, sizeof(struct sockaddr)) == -1) {
		LOG(ERROR) << "ERROR childProcess, failed to bind\n";
		exit(1);
	}
	
	//listen for client connections
	listen(sockfd, 5);	//second argument is queue size
	
	fflush(stdout);
	FD_ZERO(&fds);
	FD_ZERO(&rfds);
	FD_SET(sockfd, &fds);
	max_fds = sockfd;
	
	//infinitely loop waiting for connections and messages
	while(true) {
		rfds = fds;
		
		//monitor socket file descriptors
		if(select(max_fds+1, &rfds, NULL, NULL, NULL)  < 0) {
			LOG(ERROR) << "ERROR childProcess, failed to select";
			exit(1);
		}
		
		//iterate through file descriptors checking for connections/messages
		for(int i=0; i<=max_fds; i++) {
			if(FD_ISSET(i, &rfds)) {	//checks if filedescriptor is in read set
				if(i == sockfd) {	//if i is an incoming connection, accept it
					acceptfd = 0;
					length = sizeof(struct sockaddr_in);
					
					//accept incoming connection
					acceptfd = accept(sockfd, (struct sockaddr *) &client, &length);
					if(acceptfd < 0){
						LOG(ERROR) << "ERROR childProcess, failed to accept\n";
						exit(1);
					}
					else {
						FD_SET(acceptfd, &fds);
						if(acceptfd > max_fds) {
							max_fds = acceptfd;
						}
					}
				}
				else msgHandler(i, &fds, sockfd, max_fds);	//if i is an incoming message, call msgHandler
			}
		}
	}
	exit(1);
}

// This function handles the create command input from a client
// Returns 1 if room exists and 0 if successful
int createRoom(std::string name, int n) {
	//Make sure room doesn't already exist
	if(rooms.size() > 0) {
		for(int i=0; i<rooms.size(); i++) {
			if(rooms[i].name == name) {
				return 1;
			}
		}
	}
	
	int pid;
	pid = fork();	//create process for new chat room
	if (pid == 0) {	//child process
		childProcess(roomPort);
		return 0;
	}
	else {	//parent process
		chatRoom r(pid, roomPort, name);
		rooms.push_back(r);	//add this room to the chatRoom vector
		LOG(INFO) << "New room: " << name << std::endl;
		roomPort++;
		return 0;
	}
}

// This function handles the Delete command
// Returns 2 if the room doesn't exist and 0 if successful
int deleteRoom(std::string name, int n) {
	//make sure room exists
	if(rooms.size() > 0) {
		for(int i=0; i<=rooms.size(); i++) {
			if(strcmp(rooms[i].name.c_str(),name.c_str()) == 0) {
				kill(rooms[i].pid, SIGKILL);	//kill child process handling this room
				rooms.erase(rooms.begin() + i);	//erase this room from the chatRoom vector
				LOG(INFO) << "Deleted: " << name << std::endl;
				return 0;
			}
		}
	}
	return 2;
}

// This function handles the Join command
// Returns a 2 if the room doesn't exist and 0 if successful
int joinRoom(std::string name, int n, int i, Reply &rep) {
	//Make sure room exists
	if(rooms.size() > 0) {
		for(int j=0; j<rooms.size(); j++) {
			if(strcmp(rooms[j].name.c_str(),name.c_str()) == 0) {
                rep.num_member = rooms[j].num_mem;
                rep.port = rooms[j].port;
                rooms[j].num_mem += 1;
				LOG(INFO) << " A client has joined: " << name << std::endl;
				return 0;
			}
		}
	}
    return 2;
}

// This function handles an input command from a client
// It redirects to the appropriate helper functions depending on the command
// and returns the status of the command as an int as well as modifys the passed reply struct as needed
int cmdHandler(int i, fd_set *fds, int sockfd, int max_fds, Reply &rep) {
	int n;	//number of chars sent/recieved
	char readBuf[256];
	int stat;
	
	if((n = read(i, readBuf, sizeof(readBuf))) <= 0) {
		close(i);
		FD_CLR(i, fds);
	}
	else {
		//iterate through file descriptors for incoming commands
		for(int j=0; j<=max_fds; j++) {
			if(FD_ISSET(j, fds)) {
				if(j != i) {
					std::string cmd(readBuf, n);	//need c++ string to easily get substrings
					//CREATE room command
					if(cmd.substr(0, 6) == "CREATE") {
						stat = createRoom(cmd.substr(7,7-n), n);
						return stat;
					}
					//DELETE room command
					else if (cmd.substr(0, 6) == "DELETE") {
						stat = deleteRoom(cmd.substr(7,7-n), n);
                        return stat;
					}
					//JOIN room command
					else if (cmd.substr(0, 4) == "JOIN") {
						stat = joinRoom(cmd.substr(5,n-5), n, i, rep);
                        return stat;
					}
                    //LIST command
                    else if (cmd.substr(0,4) == "LIST"){
                        if(rooms.size() == 0){
                            strcpy(rep.list_room, "empty");
                            return 0;
                        } else{
                            std::string list = "";
                            for(int x = 0; x < rooms.size(); x++){
                                list += rooms.at(x).name.c_str();
                                if(x != rooms.size()-1){
                                    list += ",";
                                }
                            }
                            strcpy(rep.list_room,list.c_str());
                            return 0;
                        }
                        return 4;
                    }
					else {
                        LOG(ERROR) << "invalid input command entered in cmdHandler\n";
                        return 3;
                    }
				}
			}
		}
	}
    return 4;
}