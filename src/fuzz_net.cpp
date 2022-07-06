#include "fuzz_net.h"
#include "crypto/equihash.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sys/socket.h>
#include <vector>

FuzzNodes globalFuzzNodes;

FuzzNodes &FuzzZenProvider::ConsumeConnections(){

	std::vector<uint8_t> bdata = ConsumeBytes<uint8_t>(1);
	unsigned char connections_size = bdata.front();
	connections_size = max(connections_size, 125);

	printf("create %d connections\n",connections_size - 1);

	globalFuzzNodes.lock();

	for(unsigned char i = 0;i <= connections_size;i++){

		bdata = ConsumeBytes<uint8_t>(1);
		unsigned char position = bdata.front();

		printf("add connection at tick %d\n",position);
		globalFuzzNodes.addConnection(position);

	}

	globalFuzzNodes.unlock();

	
	return globalFuzzNodes;

}

int FuzzNode::connect(){
	
	int sockets[2] = {0,0};

	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0){
		perror("failed to create socket pair");
		return -1;
	}

	fuzzfd = sockets[0];
	appfd = sockets[1];

	return appfd;

}

FuzzNode *FuzzNodes::getNode(unsigned short tick){
	
	for(auto node = Nodes.begin(); node != Nodes.end(); node++){
		if(*node == tick)
			return &(*node);
	}

	return NULL;

}

unsigned short FuzzZenProvider::ConsumeShort(){
	
	std::vector<uint8_t> bdata = ConsumeBytes<uint8_t>(2);

	unsigned short ret = 0;
	ret += bdata.front();
	ret += bdata.back() << 8;

	return ret;
}

void FuzzNodes::lock(){
	mtx.lock();
}

void FuzzNodes::unlock(){
	mtx.unlock();
}

void FuzzNodes::addConnection(unsigned short p){
	Nodes.emplace_back(p);
}


