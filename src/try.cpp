
#include "fuzz_net.h"
#include <cstdint>

unsigned short FuzzZenProvider::ConsumeShort(){
	
	std::vector<uint8_t> bdata = ConsumeBytes<uint8_t>(2);

	unsigned short ret = 0;
	ret += bdata.front();
	ret += bdata.back() << 8;

	return ret;
}

int main(){
	unsigned short buffer[2] = {0x3e5a, 20};
	FuzzZenProvider reader((const uint8_t *) buffer, 2);

	unsigned short data = reader.ConsumeShort();

	printf("data = %x\n",data);
	printf("buffer[0] = %x\n",buffer[0]);

	return 0;
}


