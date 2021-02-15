#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, assert_equal, assert_true, assert_false, \
    start_nodes, stop_nodes, get_epoch_data, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import time
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
TX_FEE = Decimal("0.000567")


class sc_bwt_request(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib',
            '-logtimemicros=1']] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Node1 creates a sc1 and, after a few negative tests, some bwt requests are sent for that sc1.
        All the relevant txes (creation included) are accepted in the mempool and then mined in a block,
        with the exception of the txes with zero fee, since we started the node with the -blockprioritysize=0 option.
        Such zero fee mbtr txes are removed from the mempool whenever the safeguard of the epoch is crossed.
        A second sc is created and bwt request are sent for a zero balance sc (accepted) and a ceased sc (rejected)
        Both high level and raw versions of the command are tested.
        '''

        # cross-chain transfer amount
        creation_amount1 = Decimal("1.0")
        creation_amount2 = Decimal("2.0")

        bwt_amount = Decimal("0.5")

        blocks = [self.nodes[0].getblockhash(0)]

        mark_logs("Node 1 generates 1 block to prepare coins to spend", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates 220 block to reach sidechain height", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        #generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk1  = mcTest.generate_params("sc1")
        mbtrVk1 = mcTest.generate_params("sc1_mbtrVk")
        c1 = generate_random_field_element_hex()

        vk2 = mcTest.generate_params("sc2")
        mbtrVk2 = mcTest.generate_params("sc2_mbtrVk")
        c2 = generate_random_field_element_hex()

        vk3 = mcTest.generate_params("sc3")
        mbtrVk3 = mcTest.generate_params("sc3_mbtrVk")
        c3 = generate_random_field_element_hex()

        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        fee_cr1 = Decimal("0.0002")
        cmdInput = {
            "withdrawalEpochLength":EPOCH_LENGTH,
            "toaddress":"dada",
            "amount":creation_amount1,
            "fee":fee_cr1,
            "wCertVk":vk1,
            "constant":c1,
            "wMbtrVk": mbtrVk1,
        }

        try:
            ret = self.nodes[1].create_sidechain(cmdInput)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        # Check that wMbtrVk is duly serialized
        assert_equal(self.nodes[1].getscinfo(ret['scid'])['items'][0]['unconf wMbtrVk'], mbtrVk1)

        n1_net_bal = bal_before_sc_creation - creation_amount1 - fee_cr1

        cr_tx1 = ret['txid']
        scid1  = ret['scid']
        mark_logs("Node1 created the SC {} spending {} coins via tx {}.".format(scid1, creation_amount1, cr_tx1), self.nodes, DEBUG_MODE)
        self.sync_all()

        prev_epoch_block_hash = blocks[-1]

        fe1 = generate_random_field_element_hex()
        fe2 = generate_random_field_element_hex()
        fe3 = generate_random_field_element_hex()
        fe4 = generate_random_field_element_hex()

        pkh1 = self.nodes[0].getnewaddress("", True)
        pkh2 = self.nodes[0].getnewaddress("", True)
        pkh3 = self.nodes[0].getnewaddress("", True)
        pkh4 = self.nodes[0].getnewaddress("", True)

        p1 = mcTest.create_test_proof("sc1", 0, blocks[-2], blocks[-1], 1, fe1, [pkh1], []) 
        p2 = mcTest.create_test_proof("sc2", 0, blocks[-2], blocks[-1], 1, fe2, [pkh2], []) 
        p3 = mcTest.create_test_proof("sc2", 1, blocks[-2], blocks[-1], 3, fe3, [pkh3], []) 
        p4 = mcTest.create_test_proof("sc2", 1, blocks[-2], blocks[-1], 2, fe2, [pkh4], []) 


        #--- negative tests for bwt transfer request ----------------------------------------
        mark_logs("...performing some negative test...", self.nodes, DEBUG_MODE)

        # 1.  wrong scid
        outputs = [{'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':"abcd", 'scProof':p1, 'pubkeyhash':pkh1 }]
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, {});
            assert_true(False)
        except JSONRPCException, e:
            mark_logs(e.error['message'], self.nodes,DEBUG_MODE)

        # 2.  wrong pkh
        outputs = [{'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':scid1 }]
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, {});
            assert_true(False)
        except JSONRPCException, e:
            mark_logs(e.error['message'], self.nodes,DEBUG_MODE)

        # 3.  negative scfee
        outputs = [{'scUtxoId':fe1, 'scFee':Decimal("-0.2"), 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 }]
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, {});
            assert_true(False)
        except JSONRPCException, e:
            mark_logs(e.error['message'], self.nodes,DEBUG_MODE)

        # 4. not including one of the mandatory param
        outputs = [{'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 }]
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, {});
            assert_true(False)
        except JSONRPCException, e:
            mark_logs(e.error['message'], self.nodes,DEBUG_MODE)

        # 5.  wrong field element
        outputs = [{'scUtxoId':"abcd", 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 }]
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, {});
            assert_true(False)
        except JSONRPCException, e:
            mark_logs(e.error['message'], self.nodes,DEBUG_MODE)

        # TODO add negative tests for libzendoomc verification of proof and field element 
        print
        #--- end of negative tests --------------------------------------------------

        mark_logs("Node1 creates a tx with a single bwt request for sc", self.nodes, DEBUG_MODE)
        totScFee = Decimal("0.0")

        TX_FEE = Decimal("0.000123")
        outputs = [{'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 }]
        cmdParms = { "minconf":0, "fee":TX_FEE}

        try:
            bwt1 = self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
            mark_logs("  --> bwt_tx_1 = {}.".format(bwt1), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        totScFee = totScFee + SC_FEE

        n1_net_bal = n1_net_bal - SC_FEE - TX_FEE 

        self.sync_all()
        decoded_tx = self.nodes[1].getrawtransaction(bwt1, 1)

        assert_equal(len(decoded_tx['vmbtr_out']), 1)
        assert_equal(decoded_tx['vmbtr_out'][0]['mcDestinationAddress']['pubkeyhash'], pkh1)
        assert_equal(decoded_tx['vmbtr_out'][0]['scFee'], SC_FEE)
        assert_equal(decoded_tx['vmbtr_out'][0]['scProof'], p1)
        assert_equal(decoded_tx['vmbtr_out'][0]['scUtxoId'], fe1)
        assert_equal(decoded_tx['vmbtr_out'][0]['scid'], scid1)

        mark_logs("Node1 creates a tx with the same single bwt request for sc", self.nodes, DEBUG_MODE)
        try:
            bwt2 = self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
            mark_logs("  --> bwt_tx_2 = {}.".format(bwt2), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        totScFee = totScFee + SC_FEE

        n1_net_bal = n1_net_bal - SC_FEE - TX_FEE 
     
        mark_logs("Node0 creates a tx with a single bwt request for sc with mc_fee=0 and sc_fee=0", self.nodes, DEBUG_MODE)

        outputs = [{'scUtxoId':fe1, 'scFee':Decimal("0.0"), 'scid':scid1, 'scProof' :p1, 'pubkeyhash':pkh1 }]
        cmdParms = {"fee":0.0}
        try:
            bwt3 = self.nodes[0].request_transfer_from_sidechain(outputs, cmdParms);
            mark_logs("  --> bwt_tx_3 = {}.".format(bwt3), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        # verify we have in=out since no fees and no cc outputs
        decoded_tx = self.nodes[0].getrawtransaction(bwt3, 1)
        input_tx   = decoded_tx['vin'][0]['txid']
        input_tx_n = decoded_tx['vin'][0]['vout']
        decoded_input_tx = self.nodes[0].getrawtransaction(input_tx, 1)
        in_amount  = decoded_input_tx['vout'][input_tx_n]['value']
        out_amount = decoded_tx['vout'][0]['value']
        assert_equal(in_amount, out_amount)

        mark_logs("Node0 creates a tx with many bwt request for sc, one of them is repeated", self.nodes, DEBUG_MODE)
        fer = generate_random_field_element_hex()
        outputs = [
            {'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 },
            {'scUtxoId':fer, 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 },
            {'scUtxoId':fer, 'scFee':SC_FEE, 'scid':scid1, 'scProof':p1, 'pubkeyhash':pkh1 }
        ]
        cmdParms = {"minconf":0, "fee":TX_FEE}
        try:
            bwt4 = self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
            mark_logs("  --> bwt_tx_4 = {}.".format(bwt4), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        decoded_tx = self.nodes[0].getrawtransaction(bwt4, 1)
        assert_equal(len(decoded_tx['vmbtr_out']), 3)

        totScFee = totScFee + 3*SC_FEE
        n1_net_bal = n1_net_bal - 3*SC_FEE -TX_FEE

        # verify all bwts are in mempool together with sc creation
        assert_true(cr_tx1 in self.nodes[0].getrawmempool())
        assert_true(bwt1 in self.nodes[0].getrawmempool())
        assert_true(bwt2 in self.nodes[0].getrawmempool())
        assert_true(bwt3 in self.nodes[1].getrawmempool())
        assert_true(bwt4 in self.nodes[1].getrawmempool())

        mark_logs("Node0 confirms sc creation and bwt requests generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        curh = self.nodes[0].getblockcount()

        vtx = self.nodes[1].getblock(blocks[-1], True)['tx']
        assert_true(bwt1, bwt2 in vtx)
        assert_true(bwt2 in vtx)
        assert_true(bwt4 in vtx)

        # zero fee txes are not included in the block since we started nodes with -blockprioritysize=0
        assert_false(bwt3 in vtx)

        sc_info = self.nodes[0].getscinfo(scid1)['items'][0]

        mark_logs("Check creation and bwt requests contributed to immature amount of SC", self.nodes, DEBUG_MODE)
        # check immature amounts, both cr and btrs 
        ima = Decimal("0.0")
        for x in sc_info['immature amounts']:
            ima = ima + x['amount']
            assert_equal(x['maturityHeight'], curh + 3) 
        assert_equal(ima, totScFee + creation_amount1)

        #  create one more sc
        ret = self.nodes[0].sc_create(EPOCH_LENGTH, "dada", creation_amount2, vk2, "", c2, mbtrVk2);
        scid2  = ret['scid']
        cr_tx2 = ret['txid']
        mark_logs("Node0 created the SC2 spending {} coins via tx {}.".format(creation_amount1, cr_tx2), self.nodes, DEBUG_MODE)
        self.sync_all()

        # create a bwt request with the raw cmd version
        mark_logs("Node0 creates a tx with a bwt request using raw version of cmd", self.nodes, DEBUG_MODE)
        sc_bwt2_1 = [{'scUtxoId':fe2, 'scFee':SC_FEE, 'scid':scid2, 'scProof':p2, 'pubkeyhash':pkh2, "wMbtrVk": mbtrVk3 }]
        try:
            raw_tx = self.nodes[0].createrawtransaction([], {}, [], [], sc_bwt2_1)
            funded_tx = self.nodes[0].fundrawtransaction(raw_tx)
            signed_tx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            bwt5 = self.nodes[0].sendrawtransaction(signed_tx['hex'])
            mark_logs("  --> bwt_tx_5 = {}.".format(bwt5), self.nodes, DEBUG_MODE)

        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        decoded_tx = self.nodes[0].decoderawtransaction(signed_tx['hex'])
        assert_equal(len(decoded_tx['vmbtr_out']), 1)
        assert_equal(decoded_tx['vmbtr_out'][0]['mcDestinationAddress']['pubkeyhash'], pkh2)
        assert_equal(decoded_tx['vmbtr_out'][0]['scFee'], SC_FEE)
        assert_equal(decoded_tx['vmbtr_out'][0]['scProof'], p2)
        assert_equal(decoded_tx['vmbtr_out'][0]['scUtxoId'], fe2)
        assert_equal(decoded_tx['vmbtr_out'][0]['scid'], scid2)

        # create a bwt request with the raw cmd version with some mixed output and cc output
        mark_logs("Node0 creates a tx with a few bwt request and mixed outputs using raw version of cmd", self.nodes, DEBUG_MODE)
        outputs = { self.nodes[0].getnewaddress() :4.998 }
        sc_cr = [ {"epoch_length":10, "amount":1.0, "address":"effe", "wCertVk":vk3, "constant":c3} ]
        sc_ft = [ {"address":"abc", "amount":1.0, "scid":scid2}, {"address":"cde", "amount":2.0, "scid":scid2} ]
        sc_bwt3 = [
            {'scUtxoId':fe2, 'scFee':Decimal("0.13"), 'scid':scid1, 'scProof':p2, 'pubkeyhash':pkh2 },
            {'scUtxoId':fe3, 'scFee':Decimal("0.23"), 'scid':scid2, 'scProof':p3, 'pubkeyhash':pkh3 },
            {'scUtxoId':fe4, 'scFee':Decimal("0.12"), 'scid':scid2, 'scProof':p4, 'pubkeyhash':pkh4 }
        ]
        try:
            raw_tx = self.nodes[0].createrawtransaction([], outputs, sc_cr, sc_ft, sc_bwt3)
            funded_tx = self.nodes[0].fundrawtransaction(raw_tx)
            signed_tx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            bwt6 = self.nodes[0].sendrawtransaction(signed_tx['hex'])
            mark_logs("  --> bwt_tx_6 = {}.".format(bwt6), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        totScFee = totScFee + Decimal("0.13")

        self.sync_all()
        decoded_tx = self.nodes[0].decoderawtransaction(signed_tx['hex'])
        assert_equal(len(decoded_tx['vsc_ccout']), 1)
        assert_equal(len(decoded_tx['vft_ccout']), 2)
        assert_equal(len(decoded_tx['vmbtr_out']), 3)
        assert_equal(decoded_tx['vmbtr_out'][0]['scUtxoId'], fe2)
        assert_equal(decoded_tx['vmbtr_out'][1]['scUtxoId'], fe3)
        assert_equal(decoded_tx['vmbtr_out'][2]['scUtxoId'], fe4)

        # verify all bwts are in mempool
        assert_true(bwt3 in self.nodes[0].getrawmempool())
        assert_true(bwt5 in self.nodes[0].getrawmempool())
        assert_true(bwt6 in self.nodes[0].getrawmempool())

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        vtx = self.nodes[1].getblock(blocks[-1], True)['tx']
        assert_true(bwt5 in vtx)
        assert_true(bwt6 in vtx)

        mark_logs("Node0 generates {} blocks".format(EPOCH_LENGTH - 2), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH - 2))
        self.sync_all()

        # the zero fee mbtr tx has been removed from empool since we reached epoch safe guard
        assert_false(bwt3 in self.nodes[0].getrawmempool())

        epoch_block_hash, epoch_number = get_epoch_data(scid1, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number, epoch_block_hash), self.nodes, DEBUG_MODE)

        #empty sc1 balance
        bwt_amount = creation_amount1
        amounts = [{"pubkeyhash":pkh2, "amount":bwt_amount}]
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            0, c1, [pkh2], [bwt_amount])

        mark_logs("Node1 sends a cert withdrawing the contribution of the creation amount to the sc balance", self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[1].send_certificate(scid1, epoch_number, 0, epoch_block_hash, proof, amounts, CERT_FEE)
            mark_logs("Node 1 sent a cert with bwd transfer of {} coins to Node1 pkh via cert {}.".format(bwt_amount, cert_epoch_0), self.nodes, DEBUG_MODE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        n1_net_bal = n1_net_bal - CERT_FEE

        mark_logs("Node0 confirms cert generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Sc balance is nothing but sc fees of the various mbtr", self.nodes, DEBUG_MODE)
        sc_post_bwd = self.nodes[0].getscinfo(scid1, False, False)['items'][0]
        assert_equal(sc_post_bwd["balance"], totScFee)

        ceasing_h = int(sc_post_bwd['ceasing height'])
        current_h = self.nodes[0].getblockcount()

        mark_logs("Node0 generates {} blocks moving on the ceasing limit".format(ceasing_h - current_h - 1), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(ceasing_h - current_h - 1))
        self.sync_all()

        outputs = [{'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':scid1, 'scProof' :p1, 'pubkeyhash':pkh3 }]
        cmdParms = { "minconf":0, "fee":0.0}
        mark_logs("Node0 creates a tx with a bwt request for a sc with null balance", self.nodes, DEBUG_MODE)
        try:
            bwt7 = self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
            mark_logs("  --> bwt_tx_7 = {}.".format(bwt7), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        assert_true(bwt7 in self.nodes[0].getrawmempool());

        # zero fee txes are not included in the block since we started nodes with -blockprioritysize=0
        #totScFee = totScFee + SC_FEE
        n1_net_bal = n1_net_bal - SC_FEE 
        assert_equal(n1_net_bal, Decimal(self.nodes[1].z_gettotalbalance(0)['total']))

        mark_logs("Node0 generates 1 block, thus ceasing the scs", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid1)['items'][0]["state"], "CEASED")
        assert_equal(self.nodes[0].getscinfo(scid2)['items'][0]["state"], "CEASED")

        # check mbtr is not in the block just mined
        vtx = self.nodes[0].getblock(blocks[-1], True)['tx']
        assert_false(bwt7 in vtx)

        # and not in the mempool either
        assert_false(bwt7 in self.nodes[0].getrawmempool());

        # the scFee contribution of the removed tx has been restored to the Node balance
        n1_net_bal = n1_net_bal + SC_FEE 

        outputs = [{'scUtxoId':fe1, 'scFee':SC_FEE, 'scid':scid1, 'scProof' :p1, 'pubkeyhash':pkh1 }]
        cmdParms = {'minconf':0, 'fee':TX_FEE}
        mark_logs("Node1 creates a tx with a bwt request for a ceased sc (should fail)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        sc_pre_restart = self.nodes[0].getscinfo(scid1)['items'][0]
        assert_equal(sc_pre_restart["balance"], totScFee)

        mark_logs("Checking data persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        assert_equal(n1_net_bal, Decimal(self.nodes[1].z_gettotalbalance(0)['total']))
        sc_post_restart = self.nodes[0].getscinfo(scid1)['items'][0]
        assert_equal(sc_pre_restart, sc_post_restart)


if __name__ == '__main__':
    sc_bwt_request().main()
