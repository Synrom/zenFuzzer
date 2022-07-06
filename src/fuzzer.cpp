#include "noui.h"
#include <cstdint>
#include <stdio.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "net.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "primitives/transaction.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "crypto/common.h" 
#include "zen/utiltls.h"




#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>



#include "init.h"
#include "crypto/common.h"
#include "addrman.h"
#include "amount.h"
#ifdef ENABLE_MINING
#include "base58.h"
#endif
#include "checkpoints.h"
#include "compat/sanity.h"
#include "consensus/validation.h"
#include "httpserver.h"
#include "httprpc.h"
#include "key.h"
#include "main.h"
#include "metrics.h"
#include "miner.h"
#include "net.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "scheduler.h"
#include "txdb.h"
#include "torcontrol.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <thread>

#ifndef WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>

#include <libsnark/common/profiling.hpp>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#if ENABLE_PROTON
#include "amqp/amqpnotificationinterface.h"
#endif

#include "librustzcash.h"

#include <zen/forks/fork2_replayprotectionfork.h>

#include "fuzz_net.h"


void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

bool FuzzAppInit(int argc, const char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    ParseParameters(argc, argv);

    try
    {
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine()) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
            return false;
        }

        SoftSetBoolArg("-server", true);

        fRet = AppInitFuzzer(threadGroup, scheduler);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    return fRet;


}
// <number of COnnections><tick for connection>...<<connection choose><msg>>....

void fuzz_data(const char *data, unsigned int size){

	boost::thread_group thread_group;
	CScheduler scheduler;

	FuzzZenProvider dataReader((const uint8_t *) data,size);

	printf("read connections\n");
	FuzzNodes &Connections = dataReader.ConsumeConnections();

	printf("start node\n");

	SetupEnvironment();
	noui_connect();

	const char *argv[] = {"zend"};

	FuzzAppInit(1, argv);

}

int main(int argc, char *argv[]){

	if(argc < 2){
		printf("usage:\n");
		printf("%s <fuzz data filename>\n",argv[0]);
		return 1;
	}

	printf("asdf %s\n",argv[1]);
	std::ifstream fuzz_file(argv[1], std::ios::binary|std::ios::ate);

	std::ifstream::pos_type end_position = fuzz_file.tellg();
	int len = end_position;

	std::vector<char> bytes(len);

	fuzz_file.seekg(0, std::ios::beg);
	fuzz_file.read(bytes.data(), len);

	printf("fuzz data\n");
	fuzz_data(bytes.data(), len);

	fuzz_file.close();


	return 0;  // Non-zero return values are reserved for future use.
}



