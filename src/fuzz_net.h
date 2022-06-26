#ifndef FUZZ_NET_H
#define FUZZ_NET_H

#include "FuzzedDataProvider.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <unistd.h>
#include <vector>


class FuzzNode{
public:
	FuzzNode(unsigned short o): order_(o) {}
	~FuzzNode(){
		close(fuzzfd);
		close(appfd);
	}
	bool operator == (unsigned short cmp){
		return order() == cmp;
	}
	unsigned int order(){
		return order_;
	}

	int connect();
private:
	int fuzzfd{0},appfd{0};
	unsigned short order_{0};
};

class FuzzNodes{
public:
	void addConnection(unsigned short );
	size_t size();
	void lock();
	void unlock();
	FuzzNode *getNode(unsigned short);
private:
	std::vector<FuzzNode> Nodes;
	std::mutex mtx;
};


class FuzzZenProvider: public FuzzedDataProvider{
public:
	FuzzZenProvider(const uint8_t *data, size_t size) : FuzzedDataProvider(data,size) {}
	//CMessageHeader ConsumeMsgHeader();
	FuzzNodes &ConsumeConnections();
	unsigned short ConsumeShort();

};

extern FuzzNodes globalFuzzNodes;



#endif
