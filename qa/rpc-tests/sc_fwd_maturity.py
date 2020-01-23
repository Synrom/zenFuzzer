#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_false, assert_true, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
import traceback
import os,sys
import shutil
from random import randint
from decimal import Decimal
import logging
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1

class headers(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[['-sccoinsmaturity=2', '-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES )

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
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
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True

    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        #self.sync_all()
        time.sleep(2)
        self.is_network_split = False

    def dump_sc_info_record (self, info, i):
        if DEBUG_MODE == 0:
            return
        print "  Node %d - scid: %s" % ( i, info["scid"])
        print "    balance: %f" % (info["balance"])
        print "    created in block: %s (%d)" % (info["created in block"], info["created at block height"])
        print "    created in tx:    %s" % info["creating tx hash"]
        print "    immature amounts:  ", info["immature amounts"]
        print

    def dump_sc_info(self, scId=""):
        if scId != "":
            print "-------------------------------------------------------------------------------------"
            for i in range(0, NUMB_OF_NODES):
                try:
                    self.dump_sc_info_record( self.nodes[i].getscinfo(scId), i )
                except JSONRPCException,e:
                    print "  Node %d: ### [no such scid: %s]" % (i, scId)
        else:
            print "-------------------------------------------------------------------------------------"
            for i in range(0, NUMB_OF_NODES):
                x = self.nodes[i].getscinfo()
                for info in x:
                    self.dump_sc_info_record( info, i)
        print


    def mark_logs(self, msg):
        print msg
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):

        ''' This test creates a sidechain, forwards funds to it and then verifies
          that scinfo is updated correctly also after invalidating the chain on a node
          step by step
        '''
        # network topology: (0)--(1)--(2)

        self.mark_logs("Node 1 generates 220 block")
        self.nodes[1].generate(220)
        self.sync_all()

        # side chain id
        scid_1 = "11"
        scid_2 = "22"
        scid_3 = "33"
        scid_4 = "44"
        errorString = ""

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many  = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        #---------------------------------------------------------------------------------------
        print "Current height: ", self.nodes[2].getblockcount()

        #raw_input("Press enter to create...")
        self.mark_logs("\nNode 1 creates SC 1 with "+str(creation_amount)+" coins")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx_1 = self.nodes[1].sc_create(scid_1, 123, amounts);
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        print "Check that %f coins will be mature at h=%d" % (creation_amount, curh+2)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh+2:
                assert_equal(entry["amount"], creation_amount) 
            print "...OK"
            print

        #raw_input("Press enter to send...")
        self.mark_logs("\nNode 1 sends "+str(fwt_amount_1)+" coins to SC")
        tx = self.nodes[1].sc_send("abcd", fwt_amount_1, scid_1);
        #print "tx=" + tx
        self.sync_all()

        self.mark_logs("\nNode 1 sends 3 amounts to SC 1 (tot: "+str(fwt_amount_many) + ")")
        amounts = []
        amounts.append( {"address":"add1", "amount": fwt_amount_1, "scid": scid_1})
        amounts.append( {"address":"add2", "amount": fwt_amount_2, "scid": scid_1})
        amounts.append( {"address":"add3", "amount": fwt_amount_3, "scid": scid_1})
        tx = self.nodes[1].sc_sendmany(amounts);
        #print "tx=" + tx
        self.sync_all()

        self.mark_logs("\nNode 1 creates SC 2,3,4, all with "+str(creation_amount)+" coins")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx_2 = self.nodes[1].sc_create(scid_2, 123, amounts);
        creating_tx_2 = self.nodes[1].sc_create(scid_3, 123, amounts);
        creating_tx_2 = self.nodes[1].sc_create(scid_4, 123, amounts);
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_2), 2)
        count = 0
        print "Check that %f coins will be mature at h=%d" % (creation_amount, curh+1)
        print "Check that %f coins will be mature at h=%d" % (fwt_amount_many+fwt_amount_1, curh+2)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh+2:
                assert_equal(entry["amount"], fwt_amount_many+fwt_amount_1) 
            if entry["maturityHeight"] == curh+1:
                assert_equal(entry["amount"], creation_amount) 

        assert_equal(count, 2)
        print "...OK"
        print

        self.mark_logs("\nNode 1 sends 2 amounts to SC 2 (tot: "+str(fwt_amount_2+fwt_amount_3) + ")")
        amounts = []
        amounts.append( {"address":"add2", "amount": fwt_amount_2, "scid": scid_2})
        amounts.append( {"address":"add3", "amount": fwt_amount_3, "scid": scid_2})
        tx = self.nodes[1].sc_sendmany(amounts);
        #print "tx=" + tx
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        self.dump_sc_info()
        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        count = 0
        print "Check that %f coins will be mature at h=%d" % (fwt_amount_many+fwt_amount_1, curh+1)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh+1:
                assert_equal(entry["amount"], fwt_amount_many+fwt_amount_1) 
                count += 1

        assert_equal(count, 1)
        print "...OK"
        print

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        count = 0
        print "Check that there are no immature coins"
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        assert_equal(len(ia), 0)
        print "...OK"
        print

        self.mark_logs("\nNode 2 invalidates best block")
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash() );
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
        time.sleep(1)

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        count = 0
        print "Check that %f coins will be mature at h=%d" % (fwt_amount_many+fwt_amount_1, curh+1)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh+1:
                assert_equal(entry["amount"], fwt_amount_many+fwt_amount_1) 
                count += 1

        assert_equal(count, 1)
        print "...OK"
        print

        self.mark_logs("\nNode 2 invalidates best block")
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash() );
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
        time.sleep(1)

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        count = 0
        print "Check that %f coins will be mature at h=%d" % (creation_amount, curh+1)
        print "Check that %f coins will be mature at h=%d" % (fwt_amount_many+fwt_amount_1, curh+2)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh+2:
                assert_equal(entry["amount"], fwt_amount_many+fwt_amount_1) 
            if entry["maturityHeight"] == curh+1:
                assert_equal(entry["amount"], creation_amount) 

        assert_equal(count, 2)
        print "...OK"
        print

        self.mark_logs("\nNode 2 invalidates best block")
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash() );
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
        time.sleep(1)

        #----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print "Current height: ", curh
        self.dump_sc_info()
        self.dump_sc_info_record (self.nodes[2].getscinfo(scid_1), 2)
        print "Check that %f coins will be mature at h=%d" % (creation_amount, curh+2)
        ia = self.nodes[2].getscinfo(scid_1)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh+2:
                assert_equal(entry["amount"], creation_amount) 
            print "...OK"
            print

        self.mark_logs("\nNode 2 invalidates best block")
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash() );
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
        time.sleep(1)
        print "Current height: ", self.nodes[2].getblockcount()
        print "Checking that sc info on Node2 is not available..."
        try:
            print self.nodes[2].getscinfo(scid_1)
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        assert_equal("scid not yet created" in errorString, True);
        print "...OK"
        print
        time.sleep(1)



if __name__ == '__main__':
    headers().main()
