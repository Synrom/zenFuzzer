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
	~FuzzNode(){
		close(fuzzfd);
		close(appfd);
	}

	int connect();
	bool isOpen();
private:
	int fuzzfd{0},appfd{0};
};

class FuzzNodes{
public:
	void addConnection();
	size_t size();
	size_t sizeOpened();
	void lock();
	void unlock();
	FuzzNode *getNode();
	bool is_established();

private:
	unsigned short tick{0};
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
