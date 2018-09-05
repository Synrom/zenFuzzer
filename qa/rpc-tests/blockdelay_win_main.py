#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
import traceback
import os,sys
import shutil
from random import randint
from decimal import Decimal
import logging

import time
class blockdelay_win_main(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        # -exportdir option means we must provide a valid path to the destination folder for wallet backups
        ed0 = "-exportdir=" + self.options.tmpdir + "/node0"
        ed1 = "-exportdir=" + self.options.tmpdir + "/node1"
        ed2 = "-exportdir=" + self.options.tmpdir + "/node2"
        ed3 = "-exportdir=" + self.options.tmpdir + "/node3"
        '''
        extra_args = [["-debug","-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed0],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed1],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed2],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed3]]
        '''

        #self.nodes = start_nodes(4, self.options.tmpdir, extra_args)
        self.nodes = start_nodes(4, self.options.tmpdir)


        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of four nodes into nodes 0/1 and 2/3.        
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:3],1,True)
        sync_mempools(self.nodes[1:3])
        self.is_network_split = False


    def run_test(self):
        blocks = []

        blocks.append(nodes[0].getblockhash(0))
        print("\n\nGenesis block is: " + blocks[0])

        print("\n\nGenerating initial blockchain")
        blocks.extend(nodes[0].generate(1)) # block height 1
        self.sync_all()
        blocks.extend(nodes[1].generate(1)) # block height 2
        self.sync_all()
        blocks.extend(nodes[2].generate(1)) # block height 3
        self.sync_all()
        blocks.extend(nodes[3].generate(1)) # block height 4
        self.sync_all()
        
        print("\n\nSplit network")
        self.split_network()

        # Main chain
        print("\n\nGenerating 2 parallel chains with different length")
        blocks.extend(nodes[0].generate(6)) # block height 5 -6 -7 -8 - 9 - 10
        self.sync_all()
        blocks.extend(nodes[1].generate(6)) # block height 11-12-13-14-15-16
        self.sync_all()

        # Malicious nodes mining privately faster
        nodes[2].generate(10) # block height 5 - 6 -7 -8 -9-10 -11 12 13 14
        self.sync_all()
        nodes[3].generate(3) # block height 15 - 16 - 17
        self.sync_all()

        nodes[3].generate(3) # block height 18 - 19 - 20
        self.sync_all()

        nodes[3].generate(61) # block height 21-82
        self.sync_all()

        print("\n\nJoin network")
        raw_input("join????")
        self.join_network()
        time.sleep(2)
        print("\n\nNetwork joined")
        # main chain (0/1 nodes win)
        self.nodes[3].generate(61)

        # bad chain (2/3 nodes win)
        # self.nodes[3].generate(163)
        time.sleep(5)
        # sync_blocks(self.nodes,1,True)
        sync_mempools(nodes)

        print nodes[0].getblockcount() , len(blocks)-1
        assert nodes[0].getblockcount() <= len(blocks)-1
        #assert nodes[0].getblockcount() > len(blocks)-1


if __name__ == '__main__':
    blockdelay_win_main().main()
